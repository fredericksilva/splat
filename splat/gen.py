# Splat - splat/generator.py
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

import math
import sources
from data import Fragment
from filters import FilterChain
import _splat

class Generator(object):

    """Generator to manage sound sources.

    This abstract class provides the basic interface to run a sound generator
    and create and mix signals run with a given base frequency and start and
    end times using a given sound source function.  Its main advantage is to
    allow a Splat piece to be run with different generators or sound sources
    without rewriting the code.  It creates a :py:class:`splat.data.Fragment`
    object to store the generated and mixed sound data.  It allows arbitrary
    extra arguments to be passed to the sound source for extra flexibility.

    In order to be used, this class needs to be sub-classed to implement
    :py:meth:`splat.gen.Generator.run` with a concrete sound source.
    """

    def __init__(self, frag, filters=None):
        """The ``frag`` argument must be a :py:class:`splat.data.Fragment`
        instance.

        A chain of ``filters`` can also be initialised here with a list of
        filter functions and internally create a
        :py:meth:`splat.filters.FilterChain` object.  This can be altered later
        via :py:attr:`splat.gen.Generator.filters`.
        """
        self._frag = frag
        self._filter_chain = FilterChain(filters)
        self._levels = tuple([0.0 for x in range(self.frag.channels)])
        self._time_stretch = 1.0

    @property
    def levels(self):
        """Sound levels as a tuple in dB."""
        return self._levels

    @levels.setter
    def levels(self, values):
        if len(values) != self.frag.channels:
            raise Exception("Channels mismatch")
        self._levels = values

    @property
    def frag(self):
        """:py:class:`splat.data.Fragment` instance with the generated
        sounds."""
        return self._frag

    @property
    def channels(self):
        """Number of channels."""
        return self._frag.channels

    @property
    def sample_rate(self):
        """Sample rate in Hz."""
        return self._frag.sample_rate

    @property
    def filters(self):
        """The :py:class:`splat.filters.FilterChain` being used."""
        return self._filter_chain

    @filters.setter
    def filters(self, filters):
        self._filter_chain = FilterChain(filters)

    @property
    def time_stretch(self):
        """Time stretch factor

        All ``start`` and ``end`` times are multiplied by this value when
        calling :py:meth:`splat.gen.Generator.run`.
        """
        return self._time_stretch

    @time_stretch.setter
    def time_stretch(self, value):
        self._time_stretch = value

    def _run(self, source, start, end, *args, **kw):
        """Main method, designed to be invoked by sub-classes via
        :py:meth:`splat.gen.Generator.run`

        The ``source`` argument is a sound source function (:ref:`sources`), to
        which the ``*args`` and ``**kw`` arguments are passed on.  The sound is
        supposed to start and end at the times specified by the ``start`` and
        ``end`` arguments.
        """
        start *= self.time_stretch
        end *= self.time_stretch
        frag = Fragment(self.channels, self.sample_rate, (end - start))
        source(frag, *args, **kw)
        self.filters.run(frag)
        self.frag.mix(frag, start)

    def run(self, start, end, *args, **kw):
        """Main abstract method to be implemented by sub-classes

        This method is the main entry point to run the generator and actually
        produce some sound data.  It will typically call
        :py:meth:`splat.gen.Generator._run` with a sound source and specific
        arguments.
        """
        raise NotImplementedError


class SineGenerator(Generator):

    """Sine wave generator.

    This is the simplest generator, based on the :py:func:`splat.sources.sine`
    source to generate pure sine waves.
    """

    def run(self, freq, start, end, levels=None):
        if levels is None:
            levels = self._levels
        self._run(sources.sine, start, end, freq, levels)


class OvertonesGenerator(Generator):

    """Overtones generator.

    Overtones are defined by an ``overtones`` dictionary.  For a description of
    the overtunes, see the :py:func:`splat.sources.overtones` source which is
    used by this generator.

    Note: The time to generate the signal increases with the number of
    overtones.
    """

    def __init__(self, *args, **kw):
        super(OvertonesGenerator, self).__init__(*args, **kw)
        self.overtones = { 1.0: 0.0 }

    def ot_decexp(self, k=1.0, n=24):
        """Set harmonic overtones levels following a decreasing exponential.

        For a given ratio ``k`` and a harmonic ``i`` from 1 to a total number
        of harmonics ``n``, the amplitude of each overtone is set following
        this function:

        .. math::

           o[i] = exp\\left(\\frac{1 - i}{k}\\right)

        As a result, the fundamental frequency (overtone 1.0) always has an
        amplitude of 1.0 (0 dB).

        A higher ``k`` value means the function will decrease faster causing
        less high-frequency harmonics.
        """
        self.overtones = dict()
        for j in (float(i) for i in range(n)):
            l = _splat.lin2dB(math.exp(-j / k))
            self.overtones[j + 1] = l

    def run(self, freq, start, end, levels=None):
        if levels is None:
            levels = self._levels
        self._run(sources.overtones, start, end, freq, levels, self.overtones)
