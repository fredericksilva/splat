# Splat - splat/__init__.py
#
# Copyright (C) 2012, 2013 Guillaume Tucker <guillaume@mangoz.org>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

from _splat import lin2dB, dB2lin, sample_precision
from _splat import Signal

__all__ = ['gen', 'data', 'filters', 'sources', 'scales', 'interpol']

VERSION = (1, 1)
VERSION_STR = '.'.join([str(v) for v in VERSION])
BUILD = 7

__version__ = VERSION_STR

def check_version(ver):
    if ver != VERSION:
        raise Exception("Version mismatch: {0}, required: {1}".format(
                VERSION, ver))
