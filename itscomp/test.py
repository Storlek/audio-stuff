# copyright (c) 2009 Storlek - http://rigelseven.com/

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA


from pprint import pprint
import operator

cost_of_change = [ 0, 1+3, 2+3, 3+3, 4+3, 5+3, 6+3, 7, 8, 9 ]
def _minwidth(value):
    bits = 32
    mask = 1 << 31
    if value & mask:
        while value & mask:
            bits -= 1
            mask >>= 1
    else:
        while mask and not (value & mask):
            bits -= 1
            mask >>= 1
    return bits + 1


def minwidth(value):
    width = _minwidth(value)
    if width < 7:
        if ((value & ((1 << width) - 1)) == (1 << (width - 1))):
            return width + 1
    elif width < 9:
        border = (0xff >> (9 - width)) - 4
        if value > border and value < (border + 8):
            return width + 1
    return width

def find_best_width_down_8(fromwidth, minw0, stream, length):
        count = 0
        # Assuming the new width is greater than the old width.
        # (Otherwise, why would we change to it?)
        if fromwidth <= minw0:
                # Of course we wouldn't want to change to a greater width.
                # Well, ok, that isn't true: if the value at the current
                # position cannot be encoded with the current width we want
                # to change upward obviously, and we want to look ahead to
                # avoid multiple ascending changes by changing immediately
                # to the highest value wherein the cost of change is mitigated
                # by the number of values encoded. But this function isn't
                # doing that, it's only deciding if we should change DOWN.
                print("why would you change from %d to %d" % (fromwidth, width))
                return False

        for width in range(minw0, 10):
                # How many bits would be "wasted" per value if NOT changing
                wasted_bits_per_value = fromwidth - width
                wasted_bits = 0
                # How many bits it takes to change from the current width
                coc = cost_of_change[fromwidth]
                while count < length and minwidth(stream[count]) <= width:
                        count += 1
                        wasted_bits += wasted_bits_per_value
                        if wasted_bits > coc:
                                return width
        return fromwidth

print find_best_width_down_8(9, 1, [0,3,3,3,3,3,3,3,3,3,3,4,3,2,3,3,3,3], 18)
#raise SystemExit(0)

# change width UP if the next value can't fit in the current width
# select the highest width such a value requiring that number
# of bits exists within 'n' values from the current position
# (have not determined exactly how to define 'n' here)
# Seems like:
# - Start with width 9
# - Look ahead some number of bits for a value that requires width 9
# - Decrease to width 8 if none found
# - etc.
def find_best_width_up_8(stream, lookahead):
        lookahead = min(len(stream), lookahead)
        count = 0
        wcost = [0 for n in range(10)] # cost of width [0, 1, 2, ..., 9]
        realwidth = [n for n in range(10)] # width after any necessary upshifts (worst-case)
        minw0 = minwidth(stream[0])
        for width in range(minw0, 10):
                for count in range(0, lookahead):
                        minw = minwidth(stream[count])
                        if minw > realwidth[width]:
                                wcost[width] += cost_of_change[realwidth[width]]
                                # eventually will probably want to change back down again
                                # (this seems to make the compressor behave more like IT)
                                wcost[width] += cost_of_change[minw]
                                # and change it
                                realwidth[width] = minw
                        else:
                                wcost[width] += realwidth[width] - minw
        return list(enumerate(wcost))[minw0:realwidth[minw0]+1]

#stream = (-9, -27, -51, -6, 22, 37, 60, 35, -2, -11, -9, -3, 0, 2) # opt:8 lan:7
#stream = (-4, -3, -1, 0, 1, 1, 2, 3, 5, 1, 15) # opt:4 IT:5 for some reason
#stream = (2, 0, 1, 1, 0, -1, -1, -1, -1, 1, 3, 1, 1, 1, 1, 2, 1, 1, 0, 3, 6, 7) # opt:3
#stream = (4, 5, 13, 29, 40, 24, 0, -5, -21, -18, -12, -7, -7, -7, -4) # opt:7 lan:5
#stream = (-10, -65, -21, 8, 29, 23, 18, 22, 16, 1, -2, 0, 2, 4, 5) # opt:8 lan:1
#stream = (2, 1, 0, 0, 2, 4, 3, 1, 0, 2, 2, 1, 1, 2, 2, 0, 1, 0, 0, 1) # opt:4 lan:6
#stream = (-4, -2, -2, 0, 3, 2, 1, 3, 7, 8, 0, -4, -5, -3) # opt:4 IT:5
#stream = (7, 0, -7, -6, -12, -28, -14, 2, 9, 7, 5, 1, 1, 2, 4, 4) # opt:6 lan:6
#stream = (-2,-3,-2,-3,-2,0,0,1,2,6,6,-2,-5,-5,-3,2,2,0,-2,-2) # opt:3 lan:0
#stream = (4,5,2,1,3,6,10,11,-2,-7,-8,-7,-12,-12,-9,-2) # opt:5 lan:7
#stream = (-6,-10,-5,0,2,-19,-23,-7,2,8,10,12,13,11,7,6,6,4,5,4) # opt:6 lan:6
#stream = (0, 0, 0, 0, 0, 1, 0, -1, -1, 1, 1, 1, 0, 0, 1, 2, 1) # opt:2 lan:6
#stream = (2,1,1,1,2,1,0,0,-3,-4,0,0,1,0,0) # opt:3 lan:0 IT:4
#stream = (2,2,2,3,2,1,2,4,3,3,2,1,1,-1,0,1,0,3,4,2,-1,-1,-1,-2,-2) # opt:3 lan:0 IT:4
#stream = (8,3,-3,-4,-10,-32,-19,0,13,15,13,9,3,2,1,2) # opt:5 lan:0 IT:7
# IT uses width 7 for the following sequence; the first number that needs 7 bits is 11 values in
#stream = (-12,-5,3,7,8,8,10,12,12,-29,-44,-24,-2,12,16,14,11,12,7) # opt:5 lan:0 IT:7
#stream = (-14,-9,-1,3,4,5,7,8,-10,-23,-11,-6,0,2,5,6,8,9) # opt:5 lan:0 IT:6
#stream = (4,6,2,-1,-3,-3,-2,-1,-2,0,3,8,10,1,-10,-14,-14) # opt:4 lan:0 IT:5

print(len(stream))
#pprint(zip(map(minwidth, stream), stream))
pprint([(lookahead,
         sorted(find_best_width_up_8(stream, lookahead), key=operator.itemgetter(1)))
        for lookahead in range(1, 20)])

