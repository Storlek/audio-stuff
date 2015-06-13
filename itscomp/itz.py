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


# testing other standard compression schemes, just for curiosity's sake

from __future__ import print_function
import struct, sys, zlib, bz2
from array import array
from pprint import pprint

class ITSHeader():
        _format = '< 4s 13s B B B 26s B B L L L L L L L B B B B'
        def __init__(self, hdr):
                (self.imps, self.filename, self.gvl, self.flags, self.vol, self.name, self.cvt, self.dfp,
                self.length, self.loopbegin, self.loopend, self.c5speed, self.susloopbegin, self.susloopend,
                self.samplepointer, self.vis, self.vid, self.vir, self.vit) = struct.unpack(self._format, hdr)

        def pack(self):
                return struct.pack(self._format, self.imps, self.filename, self.gvl, self.flags, self.vol,
                self.name, self.cvt, self.dfp, self.length, self.loopbegin, self.loopend, self.c5speed,
                self.susloopbegin, self.susloopend, self.samplepointer, self.vis, self.vid, self.vir, self.vit)

FLG_EXISTS, FLG_16BIT, FLG_COMPR = 1, 2, 8
CVT_SIGNED, CVT_DELTA = 1, 4

infile = (sys.stdin if len(sys.argv) < 2 or sys.argv[1] == '-' else file(sys.argv[1]))

header = ITSHeader(infile.read(80))
if not (header.flags & FLG_EXISTS):
        print("input sample does not exist")
        sys.exit(1)
if header.flags & FLG_COMPR:
        print("compressed samples unsupported")
        sys.exit(1)
# hopefully the rest of the flags are sane!

infile.seek(header.samplepointer)
fmt, width = (('h', 2) if header.flags & FLG_16BIT else ('b', 1))
data = array(fmt, infile.read(header.length * width))
# differentiate
def delta(idata):
        odata = idata[:]
        last = 0
        for n in range(len(odata)):
                this = odata[n]
                odata[n] = (this - last) % 128
                last = this
        return odata
identity = lambda x: x

dlen = len(data)
print(list(sorted((len(compf(prepf(data).tostring())), prepn, compn)
        for prepn, prepf in [('id', identity), ('d1', delta), ('d2', lambda x: delta(delta(x)))]
        for compn, compf in [('none', identity), ('zlib', zlib.compress), ('bzip', bz2.compress)]
)))

