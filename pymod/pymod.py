#!/usr/bin/env python

from __future__ import division, print_function

import struct, sys, os, time, ossaudiodev

# -------------------------------------------------------------------------------------------------------------
# constants

NOTE_NONE = 255 # ...
NOTE_LAST = 119 # last playable note

SAMPLE_RATE = 22050 # Better quality = more processor!!

FRACBITS = 11
FRACMASK = ((1 << FRACBITS) - 1)


PERIOD_TABLE = (
#   C     C#    D     D#    E     F     F#    G     G#    A     A#    B
27392,25856,24384,23040,21696,20480,19328,18240,17216,16256,15360,14496, # 0
13696,12928,12192,11520,10848,10240, 9664, 9120, 8608, 8128, 7680, 7248, # 1
 6848, 6464, 6096, 5760, 5424, 5120, 4832, 4560, 4304, 4064, 3840, 3624, # 2
 3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1812, # 3
 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  906, # 4=0
  856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  453, # 5=1
  428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226, # 6=2
  214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113, # 7=3
  107,  101,   95,   90,   85,   80,   75,   71,   67,   63,   60,   56, # 8=4
   53,   50,   47,   45,   42,   40,   37,   35,   33,   31,   30,   28, # 9
   26,   25,   23,   22,   21,   20,   18,   17,   16,   15,   15,   14, # A
)

MOD_FINETUNE_TABLE = (
	#  0     1     2     3     4     5     6     7
	8363, 8413, 8463, 8529, 8581, 8651, 8723, 8757,
	# -8    -7    -6    -5    -4    -3    -2    -1
	7895, 7941, 7985, 8046, 8107, 8169, 8232, 8280,
)

EFFECT_NAMES = [
	('0xy', 'Arpeggio'),
	('1xx', 'Pitch slide up'),
	('2xx', 'Pitch slide down'),
	('3xx', 'Slide to note'),
	('4xy', 'Vibrato'),
	('5xy', 'Vol slide + pitch to note'),
	('6xy', 'Vol slide + vibrato'),
	('7xy', 'Tremolo'),
	('8xx', '(unused)'),
	('9xx', 'Set offset'),
	('Axy', 'Vol slide'),
	('Bxx', 'Jump to order'),
	('Cxx', 'Set volume'),
	('Dxx', 'Pattern break'),
	('Exx', '(extended)'),
	('Fxx', 'Set speed/tempo'),
	('E0x', 'Set filter'),
	('E1x', 'Fine porta up'),
	('E2x', 'Fine porta down'),
	('E3x', 'Glissando'),
	('E4x', 'Vib waveform'),
	('E5x', 'Finetune'),
	('E6x', 'Pattern loop'),
	('E7x', 'Trem waveform'),
	('E8x', 'Panning'),
	('E9x', 'Retrigger'),
	('EAx', 'Fine vol up'),
	('EBx', 'Fine vol down'),
	('ECx', 'Note cut'),
	('EDx', 'Note delay'),
	('EEx', 'Pattern delay'),
	('EFx', 'Invert loop'),
]

# -------------------------------------------------------------------------------------------------------------
# functions

def period_to_note(period):
	if not period:
		return NOTE_NONE
	for n in range(24, 132):
		if period >= PERIOD_TABLE[n]:
			return n - 24
	return NOTE_NONE

def note_to_period(note, c5speed):
	#if c5speed == 0:
	#	return 0
	return 8363 * PERIOD_TABLE[note] // c5speed

def period_to_frequency(period):
	if period == 0:
		return sys.maxsize
	return (1712*8363) // period

def asciz_truncate(s):
	if hasattr(s, 'decode'):
		# presumably it's in cp437. who really knows!
		s = s.decode('cp437', 'replace')
	return s.split('\0')[0]

# -------------------------------------------------------------------------------------------------------------
# classes

class Note:
	NOTES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
	def __init__(self, note=255, sample=0, effect=0, param=0):
		self.note, self.sample, self.effect, self.param = \
			note, sample, effect, param
	def __repr__(self):
		v = ""
		if self.note == 255:    v += "..."
		else:                   v += "%s%d" % (self.NOTES[self.note % 12], self.note // 12)
		if self.sample:         v += " %02d" % self.sample
		else:                   v += " .."
		v += " %X%02X" % (self.effect, self.param)
		return v

class Pattern:
	def __init__(self, rows, data=None):
		self.rows = rows
		if data:
			self.data = data
		else:
			self.data = [Note() for n in range(4 * rows)]
	def get_row(self, row):
		return self.data[4 * row : 4 * row + 4]
	def get_note(self, row, channel):
		return self.data[4 * row + channel]

class Sample:
	def __init__(self):
		self.name = ''
		self.length = 0
		self.c5speed = 8363
		self.volume = 64
		self.loop_start = 0
		self.loop_end = 0
		self.data = None

class MixingChannel:
	def __init__(self):
		# variables used in the mixer are first
		self.data = None # the sample data pointer, None == nothing playing
		self.length = 0 # sample length, shifted left FRACBITS
		self.loop_start = 0 # loop start, shifted left FRACBITS
		self.loop_end = 0 # loop end, shifted left FRACBITS
		# sample position
		self.inc = 0 # how much to add to the position per sample
		self.cur = 0 # position in the sample (fixed-point; shift right FRACBITS to get the byte position)
		self.volume = 0 # the sample volume
		# from here down is processing variables for effects and stuff
		self.note = NOTE_NONE
		self.frequency = 0
		self.period = 0
		self.c5speed = 0
		self.sample = 0 # sample index, 0 = no sample
		self.offset = 0 # sample offset (9xx)
		self.volume_slide = 0 # previous volume slide (5/6/Axx)
	def set_frequency(self, frequency):
		self.inc = (frequency << FRACBITS) // SAMPLE_RATE
	def set_period(self, period):
		self.set_frequency(period_to_frequency(period))
	def copy_sample(self, sample):
		self.data = sample.data
		self.length = sample.length << FRACBITS
		if sample.loop_end:
			self.loop_start = sample.loop_start << FRACBITS
			self.loop_end = sample.loop_end << FRACBITS
			self.get_sample = self.get_sample__loop
		else:
			self.get_sample = self.get_sample__noloop
	def set_volume(self, volume):
		self.volume = max(0, min(64, volume))

	def get_sample__loop(self):
		# get the sample
		s = self.data[self.cur >> FRACBITS]
		# increment the position
		self.cur += self.inc
		if self.loop_end and self.cur >= self.loop_end:
			# loop it
			self.cur = self.loop_start | (self.cur & FRACMASK)
		elif self.cur >= self.length:
			# kill it
			self.data = None
		# finally, send it back
		return (s * self.volume)
	def get_sample__noloop(self):
		# get the sample
		s = self.data[self.cur >> FRACBITS]
		# increment the position
		self.cur += self.inc
		if self.cur >= self.length:
			# kill it
			self.data = None
		# finally, send it back
		return (s * self.volume)

# -------------------------------------------------------------------------------------------------------------
# main screen turn on !!

class FileFormatError(Exception): pass

class Song:
	def print_orderlist(self):
		print("Orderlist:")
		# the 8 here refers to 8 rows of orders (and yet, they're printed in columns :)
		for n in range(8):
			line = self.orderlist[n::8]
			print("\t" + " | ".join(["%03d" % p for p in line]))
	def print_row(self, row, data, bol=""):
		print(bol + "%02d |" % row, " | ".join([repr(note) for note in data]))
	def print_pattern(self, p):
		print("Pattern %d:" % p)
		p = self.patterns[p]
		for row in range(p.rows):
			self.print_row(row, p.get_row(row), "\t")
	def print_samples(self):
		print("Samples:")
		print("\t## Name                   Length C5Spd Vl LStart L.End.")
		print("\t-- ---------------------- ------ ----- -- ------ ------")
		for n in range(31, 0, -1):
			s = self.samples[n]
			if s.length > 0 or s.volume > 0 or s.name.strip() != '':
				smax = n
				break
		else:
			smax = 31
		for n in range(1, smax + 1):
			s = self.samples[n]
			print("\t%2d %-22s %6d %5d %2d %6d %6d" % (
				n, s.name, s.length, s.c5speed,
				s.volume, s.loop_start, s.loop_end
			))
	def print_effect_warnings(self):
		if self.effect_warnings:
			print('\033[31;1m%d unhandled effects:\033[0m' % (sum(self.effect_warnings.values())))
			for v, k in sorted(((v, k) for k, v in self.effect_warnings.items()), reverse=True):
				print('\t[%5d] %s' % (v, k))
	# the actual mod loader
	def __init__(self, f):
		# check the tag
		f.seek(1080)
		tag = f.read(4)
		if tag != b"M.K.":
			raise FileFormatError("Not an M.K. module")
		
		self.num_channels = 4 # the number of channels in the song
		
		# song title
		f.seek(0)
		self.title = asciz_truncate(f.read(20))
		
		# sample headers
		self.samples = [None] # so that it's one-based
		for n in range(31):
			data = f.read(30)
			s = Sample()
			data = struct.unpack('>22s H b b H H', data)
			s.name = asciz_truncate(data[0])
			s.length = data[1] * 2
			s.c5speed = MOD_FINETUNE_TABLE[data[2] & 0xf]
			s.volume = data[3]
			if data[5] > 2:
				s.loop_start = data[4] * 2
				s.loop_end = (data[4] + data[5]) * 2
			self.samples.append(s)
		
		# orderlist
		nord = struct.unpack('B', f.read(1))[0]
		f.read(1) # restart position (unused)
		tmp = struct.unpack('128B', f.read(128))
		npat = max(tmp)
		# throw out the irrelevant orders
		self.orderlist = tmp[:nord]
		
		# we're back to the tag - skip it
		f.read(4)
		
		# patterns
		self.patterns = []
		self.effect_warnings = {}
		def warn_check(effect, param):
			if effect in {1, 2, 3, 4, 7, 0xb, 0xe} or (effect == 0 and param != 0):
				k = ' '.join(EFFECT_NAMES[(param >> 4) + 16 if effect == 0xe else effect])
				self.effect_warnings.setdefault(k, 0)
				self.effect_warnings[k] += 1
		for pat in range(npat + 1):
			size = 64 * 4 * self.num_channels
			modpattern = struct.unpack("%dB" % size, f.read(size))
			notes = []
			for n in range(64 * self.num_channels):
				a,b,c,d = modpattern[4 * n : 4 * n + 4]
				notes.append(Note(
					note=period_to_note(((a & 0xf) << 8) | b),
					sample=(a & 0xf0) | (c >> 4),
					effect=(c & 0xf),
					param=d))
				warn_check(c & 0xf, d)
			self.patterns.append(Pattern(64, notes))
		
		# sample data
		for n in range(1, 32):
			length = self.samples[n].length
			if length:
				# bleh. this takes nearly a second
				self.samples[n].data = struct.unpack("%db" % length, f.read(length))

	# -----------------------------------------------------------------------------------------------------

	def set_tick_timer(self):
		self.mix_samples_per_tick = SAMPLE_RATE // (2 * self.mix_bpm // 5)

	def set_order(self, order, row=0):
		self.mix_process_row = 999
		self.mix_process_order = order - 1
		self.mix_break_row = row
		self.mix_tick = 1
		self.set_tick_timer()
	def reset(self):
		self.mix_bpm = 125
		self.mix_speed = 6
		self.mix_samples_left = 0
		self.set_order(0)
		self.mix_channels = [MixingChannel() for n in range(4)]

	# handle a new note/sample
	def process_note(self, chan, note):
		if note.sample:
			# set the sample and volume
			if self.samples[note.sample].data is None:
				# hum, trying to play a nonexistent sample. treat it like a note cut
				self.sample = 0
				return
			if chan.sample != note.sample or chan.data is None:
				# sample changed
				chan.cur = 0
				# reset the period and stuff?
				chan.sample = note.sample
				chan.copy_sample(self.samples[chan.sample])
			# the volume will be processed later with the
			# effects; for now, just use the sample volume
			chan.set_volume(self.samples[chan.sample].volume)
		if note.note <= NOTE_LAST:
			chan.note = note.note
			chan.c5speed = self.samples[chan.sample].c5speed
			chan.set_period(note_to_period(chan.note, chan.c5speed))
			# reset the position
			chan.cur = 0
	def process_fx_tick0(self, chan, note):
		effect, param = note.effect, note.param
		
		if effect == 0x0: # nothing/arpeggio
			pass
		elif effect == 0x9: # offset
			if note.note <= NOTE_LAST:
				if param:
					chan.offset = param << 8
				chan.cur = chan.offset << FRACBITS
		elif effect == 0x5: # volume slide + continue pitch slide
			if param:
				chan.volume_slide = (param >> 4) - (param & 0xf)
		elif effect == 0x6: # volume slide + continue vibrato
			if param:
				chan.volume_slide = (param >> 4) - (param & 0xf)
		elif effect == 0xa: # volume slide
			if param:
				chan.volume_slide = (param >> 4) - (param & 0xf)
		elif effect == 0xc: # set volume
			chan.set_volume(param)
		elif effect == 0xd: # pattern break
			tmp = (param >> 4) * 10 + (param & 0xf)
			self.mix_process_row = 999
			self.mix_break_row = tmp
		elif effect == 0xf: # speed/tempo
			if param == 0:
				pass
			elif param < 0x20:
				self.mix_speed = param
			else:
				self.mix_bpm = param
				self.set_tick_timer()
			self.mix_tick = self.mix_speed
	def process_fx_tickN(self, chan, note):
		effect, param = note.effect, note.param
		
		if effect == 0x0: # nothing/arpeggio
			pass
		elif effect == 0x5: # volume slide + continue pitch slide
			chan.set_volume(chan.volume + chan.volume_slide)
		elif effect == 0x6: # volume slide + continue vibrato
			chan.set_volume(chan.volume + chan.volume_slide)
		elif effect == 0xa: # volume slide
			chan.set_volume(chan.volume + chan.volume_slide)
	# 1. advance the tick counter
	# 2. increment the position and play notes if it's tick zero
	# 3. handle effects
	def process_tick(self):
		self.mix_tick -= 1
		# handle pattern delay and stuff
		if self.mix_tick:
			for chan, note in zip(self.mix_channels, self.mix_row_data):
				self.process_fx_tickN(chan, note)
		else:
			self.mix_process_row += 1
			if self.mix_process_row >= 64: # however many rows in the pattern
				self.mix_process_row = self.mix_break_row
				self.mix_break_row = 0
				self.mix_process_order += 1
				if self.mix_process_order >= len(self.orderlist):
					return False
				print("      -+ ( (  Order: %03d    Pattern: %03d  ) ) +-" % (
					self.mix_process_order,
					self.orderlist[self.mix_process_order]
				))
			self.mix_order = self.mix_process_order
			# this will just die at the end of the song
			self.mix_pattern = self.orderlist[self.mix_order]
			self.mix_pattern_data = self.patterns[self.mix_pattern]
			# to catch stuff like a pattern break to row 80
			if self.mix_process_row >= 64:
				self.mix_process_row = 0
			self.mix_row = self.mix_process_row
			self.mix_row_data = self.mix_pattern_data.get_row(self.mix_row)
			self.print_row(self.mix_row, self.mix_row_data)
			self.mix_tick = self.mix_speed
			for chan, note in zip(self.mix_channels, self.mix_row_data):
				self.process_note(chan, note)
				self.process_fx_tick0(chan, note)
		return True

	def read(self, buffer):
		buffer_left = len(buffer)
		pos = 0
		while buffer_left:
			if self.mix_samples_left == 0:
				if not self.process_tick():
					return 0
				self.mix_samples_left = self.mix_samples_per_tick
			# keep going until the buffer runs out or until the
			# next tick, whichever comes first
			while buffer_left and self.mix_samples_left:
				p = 0
				for channel in self.mix_channels:
					if channel.data is not None:
						p += channel.get_sample()
				buffer[pos] = p
				pos += 1
				buffer_left -= 1
				self.mix_samples_left -= 1
		return pos
#

try:
	filename = sys.argv[1]
except:
	print("usage: %s blahblah.mod" % sys.argv[0], file=sys.stderr)
	raise SystemExit(1)
song = Song(open(filename, 'rb'))

print("%s: \"%s\" (%d orders, %d patterns)" % (
	filename, song.title, len(song.orderlist), len(song.patterns)
))
#song.print_orderlist()
#song.print_pattern(song.orderlist[0])
song.print_samples()
song.print_effect_warnings()

song.reset()

# audio buffer; size should be even
mbuf = list(range(1024))

dev = ossaudiodev.open("w")
dev.setparameters(ossaudiodev.AFMT_S16_LE, 1, SAMPLE_RATE, True)
try:
	while song.read(mbuf) > 0:
		# this seems to be the fastest way to clip the buffer,
		# but it still takes about .3s for each second of data
		#def clip(s): return min(32767, max(-32768, s))
		#dev.write(struct.pack("%dh" % len(mbuf), *map(clip, mbuf)))
		dev.write(struct.pack("%dh" % len(mbuf), *mbuf))
except KeyboardInterrupt:
	pass

