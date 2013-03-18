Splat!
======

Quick intro
-----------

**Splat is a programme** to generate some audio data which you may call music.
It's written in Python to make it easy to use, and all the crucial processing
parts are implemented in C for fast number crunshing.  It's distributed under
the terms of the **GNU LGPL v3** so you remain a free individual when you use
it.

**Splat is not a programme** to generate **live** audio in real time, at least
not at the moment.  So it's better suited to the studio than to the stage.

To install it, clone the repository and run::

    python setup.py install

Then to generate the included piece called "Dew Drop" with an optional reverb::

    python dew_drop.py reverb

This creates ``dew_drop.wav`` which you can now play with your favourite
player.


Beep
----

Here's a very small **Splat** example which creates a beep 440Hz sound:

.. code-block:: python

    import splat

    gen = splat.SineGenerator(splat.Fragment(2, 48000, 1.0),
                              [splat.filters.linear_fade])
    gen.run(440.0, 0.1, 0.9)
    gen.frag.save_to_file("A440.wav")


`verdigris.mu <http://verdigris.mu>`_
-------------------------------------

You can find some pieces created with **Splat** as well as other music,
software and electronics things on `verdigris.mu <http://verdigris.mu>`_.  You
can send your creations to `info@verdigris.mu <mailto:info@verdigris.mu>`_.