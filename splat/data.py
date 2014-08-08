# Splat - splat/data.py
#
# Copyright (C) 2012, 2013, 2014 Guillaume Tucker <guillaume@mangoz.org>
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

import os
import struct
import wave
import md5
import _splat
import splat

# Used in Splat Audio Fragment files (.saf)
SAF_MAGIC = 'Splat!'
SAF_FORMAT = 1

# -----------------------------------------------------------------------------
# Utilities

def _get_fmt(f, fmt):
    if fmt is None:
        if isinstance(f, str):
            fmt = f.rpartition(os.extsep)[2].lower()
        else:
            raise Exception("Format required with file objects")
    return fmt

def _read_chunks(frag, readf, sample_w, frame_size):
    rem = len(frag)
    cur = 0
    chunk_size = (64 * 1024) / frame_size

    while rem > 0:
        n = chunk_size if (rem >= chunk_size) else rem
        raw_bytes = bytearray(readf(n))
        frag.import_bytes(raw_bytes, cur, sample_w, frag.rate, frag.channels)
        rem -= n
        cur += n

# -----------------------------------------------------------------------------
# Audio file openers

def open_wav(wav_file, fmt=None):
    if _get_fmt(wav_file, fmt) != 'wav':
        return None

    w = wave.open(wav_file, 'rb')
    channels = w.getnchannels()
    n_frames = w.getnframes()
    rate = w.getframerate()
    sample_width = w.getsampwidth()
    frag = Fragment(channels, rate, length=n_frames)
    _read_chunks(frag, w.readframes, sample_width, (sample_width * channels))
    w.close()
    return frag

def open_saf(saf_file, fmt=None):
    if _get_fmt(saf_file, fmt) != 'saf':
        return None

    is_str = isinstance(saf_file, str)
    f = open(saf_file, 'r') if is_str else saf_file
    if f.readline().strip('\n') != SAF_MAGIC:
        return None
    attr_str = f.readline().strip('\n')
    attr = {k: v for k, v in (kv.split('=') for kv in attr_str.split(' '))}
    opts = ['rate', 'channels', 'length', 'precision', 'format']
    rate, channels, length, precision, fmt = (int(attr[x]) for x in opts)
    if fmt > SAF_FORMAT:
        raise Exception("Format is more recent than this version of Splat")
    if precision != splat.sample_precision:
        raise Exception("Sample precision mismatch")
    frag = Fragment(rate=rate, channels=channels, length=length)
    frame_size = channels * splat.sample_precision / 8
    _read_chunks(frag, (lambda x: f.read(x * frame_size)), 0, frame_size)
    if is_str:
        f.close()
    if frag.md5() != attr['md5']:
        raise Exception("Fragment MD5 sum mismatch")
    return frag

audio_file_openers = [open_wav, open_saf,]

# -----------------------------------------------------------------------------
# Audio file savers

def save_wav(wav_file, frag, start=None, end=None, sample_width=2):
    w = wave.open(wav_file, 'w')
    w.setnchannels(frag.channels)
    w.setsampwidth(sample_width)
    w.setframerate(frag.rate)
    w.setnframes(len(frag))
    args = (sample_width,)
    if start is not None:
        args += (start,)
    if end is not None:
        args += (end,)
    raw_bytes = frag.as_bytes(*args)
    w.writeframes(buffer(raw_bytes))
    w.close()

# Splat Audio Fragment
def save_saf(saf_file, frag, start=None, end=None):
    is_str = isinstance(saf_file, str)
    f = open(saf_file, 'w') if is_str else saf_file
    attrs = {
        'version': splat.VERSION_STR,
        'build': splat.BUILD,
        'format': SAF_FORMAT,
        'channels': frag.channels,
        'rate': frag.rate,
        'precision': splat.sample_precision,
        'length': len(frag),
        'md5': frag.md5(),
        }
    h = ' '.join('='.join(str(x) for x in kv) for kv in attrs.iteritems())
    f.write(SAF_MAGIC + '\n')
    f.write(h + '\n')
    f.write(buffer(frag.as_bytes()))
    if is_str:
        f.close()

audio_file_savers = { 'wav': save_wav, 'saf': save_saf, }

# -----------------------------------------------------------------------------
# Data classes

class Fragment(_splat.Fragment):

    """A fragment of sound data

    Create an empty sound fragment with the given number of ``channels``,
    sample ``rate`` in Hertz and initial ``duration`` in seconds or ``length``
    in number of samples per channel.  The default number of channels is 2 and
    the default sample rate is 48000.  If no duration is specified, the
    fragment will be empty.  All the samples are initialised to 0 (silence).

    All Splat sound data is contained in :py:class:`splat.data.Fragment`
    objects.  They are accessible as a mutable sequence of tuples of floating
    point values to represent the samples of the audio channels.  The length of
    each sample tuple is equal to the number of channels of the fragment, the
    maximum being fixed to 16.

    .. note::

       It is rather slow to access and modify all the samples of a Fragment
       using the standard Python sequence interface.  The underlying
       implementation in C can handle all common data manipulations much
       faster using the Fragment methods documented below.
    """

    @classmethod
    def open(cls, in_file, fmt=None):
        """Open a file to create a sound fragment by importing audio data.

        Open a sound file specified by ``in_file``, which can be either a file
        name or a file-like object, and import its contents into a new
        :py:class:`splat.data.Fragment` instance, which is then returned.

        The format can be specified via the ``fmt`` argument, otherwise it may
        be automatically detected whenever possible.  When using file-like
        objects, it is necessary to specify the format explicitly when the
        format could only be automatically detected.  Only a limited set of
        :ref:`audio_files` formats are supported (currently only ``wav`` and
        ``saf``).  All the samples are converted to floating point values.
        """
        fmt = _get_fmt(in_file, fmt)
        for opener in audio_file_openers:
            frag = opener(in_file, fmt)
            if frag is not None:
                return frag

        raise Exception("Unsupported file format")

    def dup(self):
        """Duplicate this fragment into a new one and return it."""
        dup_frag = Fragment(channels=self.channels, rate=self.rate)
        dup_frag.mix(self)
        return dup_frag

    def md5(self, sample_width=2, as_md5_obj=False):
        """Get the MD5 checksum of all this fragment's data.

        The data is first converted to integer samples with ``sample_width`` in
        bytes, which is 2 by default for 16-bit samples.  Then the MD5 checksum
        is return as a string unless ``as_md5_obj`` is set to True in which
        case an ``md5`` object is returned."""
        md5sum = md5.new(self.as_bytes(sample_width))
        return md5sum if as_md5_obj is True else md5sum.hexdigest()

    def n2s(self, n):
        """Convert a sample index number ``n`` into a time in seconds."""
        return float(n) / self.rate

    def s2n(self, s):
        """Convert a time in seconds ``s`` into a sample index number."""
        return int(s * self.rate)

    def save(self, out_file, fmt=None, start=None, end=None, *args, **kw):
        """Save the contents of the audio fragment into a file.

        If ``out_file`` is a string, a file is created with this name;
        otherwise, it must be a file-like object.  The contents of the audio
        fragment are written to this file.  It is possible to save only a part
        of the fragment using the ``start`` and ``end`` arguments with times in
        seconds.

        The ``fmt`` argument is a string to identify the output file format to
        use.  If ``None``, the file name extension is used.  It may otherwise
        be a file-like object, in which case the format needs to be specified.
        Extra arguments that are specific to the file format may be added.

        The ``wav`` format accepts an extra ``sample_width`` argument to
        specify the number of bytes per sample for each channel, which is 2 by
        default (16 bits).
        """
        fmt = _get_fmt(out_file, fmt)
        saver = audio_file_savers.get(fmt, None)
        if saver is None:
            raise Exception("Unsupported file format: {0}".format(fmt))
        saver(out_file, self, start, end, *args, **kw)
