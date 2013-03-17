#include <Python.h>
#include <math.h>

#define BASE_TYPE_FLAGS (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE)
#define MAX_CHANNELS 16

#define lin2dB(level) (20 * log10(level))
#define dB2lin(dB) (pow10((dB) / 20))

/* ----------------------------------------------------------------------------
 * Fragment class
 */

struct Fragment_object {
	PyObject_HEAD;
	int init;
	unsigned n_channels;
	unsigned rate;
	Py_ssize_t length;
	float *data[MAX_CHANNELS];
};
typedef struct Fragment_object Fragment;

static PyTypeObject geomusic_FragmentType;

static void Fragment_dealloc(Fragment *self)
{
	if (self->init) {
		unsigned i;

		for (i = 0; i < self->n_channels; ++i)
			PyMem_Free(self->data[i]);

		self->init = 0;
	}

	self->ob_type->tp_free((PyObject *)self);
}

static int Fragment_init(Fragment *self, PyObject *args, PyObject *kw)
{
	unsigned n_channels;
	unsigned rate;
	double duration = 0.0;

	unsigned i;
	size_t length;
	size_t data_size;

	if (!PyArg_ParseTuple(args, "II|d", &n_channels, &rate, &duration))
		return -1;

	if (duration < 0.0) {
		PyErr_SetString(PyExc_ValueError, "negative duration");
		return -1;
	}

	if (n_channels > MAX_CHANNELS) {
		PyErr_SetString(PyExc_ValueError,
				"exceeding maximum number of channels");
		return -1;
	}

	length = duration * rate;
	data_size = length * sizeof(float);

	for (i = 0; i < n_channels; ++i) {
		if (!data_size) {
			self->data[i] = NULL;
		} else {
			self->data[i] = PyMem_Malloc(data_size);

			if (self->data[i] == NULL) {
				PyErr_NoMemory();
				return -1;
			}

			memset(self->data[i], 0, data_size);
		}
	}

	self->n_channels = n_channels;
	self->rate = rate;
	self->length = length;
	self->init = 1;

	return 0;
}

static PyObject *Fragment_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
	Fragment *self;

	self = (Fragment *)type->tp_alloc(type, 0);

	if (self == NULL)
		return PyErr_NoMemory();

	self->init = 0;

	return (PyObject *)self;
}

/* Fragment sequence interface */

static Py_ssize_t Fragment_sq_length(Fragment *self)
{
	return self->length;
}

static PyObject *Fragment_sq_item(Fragment *self, Py_ssize_t i)
{
	PyObject *sample;
	unsigned c;

	if ((i < 0) || (i >= self->length)) {
		PyErr_SetString(PyExc_IndexError, "index out of range");
		return NULL;
	}

	sample = PyTuple_New(self->n_channels);

	if (sample == NULL)
		return PyErr_NoMemory();

	for (c = 0; c < self->n_channels; ++c) {
		const double s = (double)self->data[c][i];
		PyTuple_SetItem(sample, c, PyFloat_FromDouble(s));
	}

	return sample;
}

static int Fragment_sq_ass_item(Fragment *self, Py_ssize_t i, PyObject *v)
{
	Py_ssize_t c;

	if (!PyTuple_CheckExact(v)) {
		PyErr_SetString(PyExc_TypeError, "item must be a tuple");
		return -1;
	}

	if (PyTuple_GET_SIZE(v) != self->n_channels) {
		PyErr_SetString(PyExc_ValueError, "channels number mismatch");
		return -1;
	}

	if ((i < 0) || (i >= self->length)) {
		PyErr_SetString(PyExc_IndexError, "set index error");
		return -1;
	}

	for (c = 0; c < self->n_channels; ++c) {
		PyObject *s = PyTuple_GET_ITEM(v, c);

		if (!PyFloat_CheckExact(s)) {
			PyErr_SetString(PyExc_TypeError,
					"item must contain floats");
			return -1;
		}

		self->data[c][i] = (float)PyFloat_AS_DOUBLE(s);
	}

	return 0;
}

static PySequenceMethods Fragment_as_sequence = {
	(lenfunc)Fragment_sq_length, /* sq_length */
	(binaryfunc)0, /* sq_concat */
	(ssizeargfunc)0, /* sq_repeat */
	(ssizeargfunc)Fragment_sq_item, /* sq_item */
	(ssizessizeargfunc)0, /* sq_slice */
	(ssizeobjargproc)Fragment_sq_ass_item, /* sq_ass_item */
	(ssizessizeobjargproc)0, /* sq_ass_slice */
	(objobjproc)0, /* sq_contains */
	(binaryfunc)0, /* sq_inplace_concat */
	(ssizeargfunc)0, /* sq_inplace_repeat */
};

/* Fragment getsetters */

static PyObject *Fragment_get_sample_rate(Fragment *self, void *_)
{
	return Py_BuildValue("I", self->rate);
}

static PyObject *Fragment_get_duration(Fragment *self, void *_)
{
	return Py_BuildValue("f", (float)self->length / self->rate);
}

static PyObject *Fragment_get_channels(Fragment *self, void *_)
{
	return Py_BuildValue("I", self->n_channels);
}

static PyGetSetDef Fragment_getsetters[] = {
	{ "sample_rate", (getter)Fragment_get_sample_rate, NULL, NULL },
	{ "duration", (getter)Fragment_get_duration, NULL, NULL },
	{ "channels", (getter)Fragment_get_channels, NULL, NULL },
	{ NULL }
};

/* Fragment methods */

static int do_resize(Fragment *self, size_t length)
{
	size_t start;
	size_t data_size;
	size_t i;

	if (length <= self->length)
		return 0;

	start = self->length * sizeof(float);
	data_size = length * sizeof(float);

	for (i = 0; i < self->n_channels; ++i) {
		if (self->data[i] == NULL)
			self->data[i] = PyMem_Malloc(data_size);
		else
			self->data[i] = PyMem_Realloc(self->data[i],data_size);

		if (self->data[i] == NULL) {
			PyErr_NoMemory();
			return -1;
		}

		memset(&self->data[i][self->length], 0, (data_size - start));
	}

	self->length = length;

	return 0;
}

static PyObject *Fragment_resize(Fragment *self, PyObject *args)
{
	Py_ssize_t length;

	if (!PyArg_ParseTuple(args, "n", &length))
		return NULL;

	if (do_resize(self, length) < 0)
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(Fragment_mix_doc,
"mix(fragment, start=0.0)\n"
"\n"
"Mix the given other ``fragment`` data into this instance by simply adding "
"the data together.  If specified, the other ``fragment`` data can be offset "
"to the given ``start`` time in seconds.\n");

static PyObject *Fragment_mix(Fragment *self, PyObject *args)
{
	Fragment *frag;
	double start = 0.0;

	size_t start_sample;
	size_t total_length;
	unsigned c;

	if (!PyArg_ParseTuple(args, "O!|d", &geomusic_FragmentType, &frag,
			      &start))
		return NULL;

	if (frag->n_channels != self->n_channels) {
		PyErr_SetString(PyExc_ValueError, "channels number mismatch");
		return NULL;
	}

	if (frag->rate != self->rate) {
		PyErr_SetString(PyExc_ValueError, "sample rate mismatch");
		return NULL;
	}

	start_sample = start * self->rate;
	total_length = start_sample + frag->length;

	if (do_resize(self, total_length) < 0)
		return NULL;

	for (c = 0; c < self->n_channels; ++c) {
		const float *src = frag->data[c];
		float *dst =  &self->data[c][start_sample];
		Py_ssize_t i = frag->length;

		while (i--)
			*dst++ += *src++;
	}

	Py_RETURN_NONE;
}

static PyObject *Fragment_import_bytes(Fragment *self, PyObject *args)
{
	PyObject *bytes_obj;
	int start;
	unsigned sample_width;
	unsigned sample_rate;
	unsigned n_channels;

	const char *bytes;
	Py_ssize_t n_bytes;
	unsigned bytes_per_sample;
	unsigned n_samples;
	unsigned end;
	unsigned ch;

	if (!PyArg_ParseTuple(args, "O!iIII", &PyByteArray_Type, &bytes_obj,
			      &start, &sample_width, &sample_rate,
			      &n_channels))
		return NULL;

	if (sample_width != 2) {
		PyErr_SetString(PyExc_ValueError, "unsupported sample width");
		return NULL;
	}

	if (n_channels != self->n_channels) {
		PyErr_SetString(PyExc_ValueError, "wrong number of channels");
		return NULL;
	}

	if (sample_rate != self->rate) {
		PyErr_SetString(PyExc_ValueError, "wrong sample rate");
		return NULL;
	}

	n_bytes = PyByteArray_Size(bytes_obj);
	bytes_per_sample = n_channels * sample_width;

	if (n_bytes % bytes_per_sample) {
		PyErr_SetString(PyExc_ValueError, "invalid buffer length");
		return NULL;
	}

	bytes = PyByteArray_AsString(bytes_obj);
	n_samples = n_bytes / bytes_per_sample;
	end = start + n_samples;

	if (do_resize(self, end) < 0)
		return NULL;

	for (ch = 0; ch < self->n_channels; ++ch) {
		const void *in = bytes + (sample_width * ch);
		float *out = &self->data[ch][start];
		unsigned s;

		for (s = start; s < end; ++s) {
			*out++ = *(int16_t *)in / 32678.0;
			in += bytes_per_sample;
		}
	}

	Py_RETURN_NONE;
}

static PyObject *Fragment_as_bytes(Fragment *self, PyObject *args)
{
	unsigned sample_width;

	PyObject *bytes_obj;
	Py_ssize_t bytes_size;
	const float *it[MAX_CHANNELS];
	unsigned i;
	unsigned c;
	char *out;

	if (!PyArg_ParseTuple(args, "I", &sample_width))
		return NULL;

	if (sample_width != 2) {
		PyErr_SetString(PyExc_ValueError, "unsupported sample width");
		return NULL;
	}

	bytes_obj = PyByteArray_FromStringAndSize("", 0);

	if (bytes_obj == NULL)
		return PyErr_NoMemory();

	bytes_size = self->length * self->n_channels * sample_width;

	if (PyByteArray_Resize(bytes_obj, bytes_size))
		return PyErr_NoMemory();

	for (c = 0; c < self->n_channels; ++c)
		it[c] = self->data[c];

	out = PyByteArray_AS_STRING(bytes_obj);

	for (i = 0; i < self->length; ++i) {
		for (c = 0; c < self->n_channels; ++c) {
			const float z = *(it[c]++);
			int16_t s;

			if (z < -1.0)
				s = -32767;
			else if (z > 1.0)
				s = 32767;
			else
				s = z * 32767;

			*out++ = s & 0xFF;
			*out++ = (s >> 8) & 0xFF;
		}
	}

	return bytes_obj;
}

PyDoc_STRVAR(Fragment_normalize_doc,
"normalize(level)\n"
"\n"
"Normalize the fragment data to the given ``level`` in dB.\n");

static PyObject *Fragment_normalize(Fragment *self, PyObject *args)
{
	double level;

	double average[MAX_CHANNELS];
	unsigned c;
	double peak;
	double gain;

	if (!PyArg_ParseTuple(args, "d", &level))
		return NULL;

	if (self->n_channels > MAX_CHANNELS) {
		PyErr_SetString(PyExc_ValueError, "too many channels");
		return NULL;
	}

	level = dB2lin(level);
	peak = 0.0;

	for (c = 0; c < self->n_channels; ++c) {
		float * const chan_data = self->data[c];
		const float * const end = &chan_data[self->length];
		const float *it;
		double avg = 0.0;
		float neg = 1.0;
		float pos = -1.0;
		float chan_peak;

		for (it = self->data[c]; it != end; ++it) {
			avg += *it / self->length;

			if (*it > pos)
				pos = *it;

			if (*it < neg)
				neg = *it;
		}

		average[c] = avg;
		neg -= avg;
		neg = fabsf(neg);
		pos -= avg;
		pos = fabsf(pos);
		chan_peak = (neg > pos) ? neg : pos;

		if (chan_peak > peak)
			peak = chan_peak;
	}

	gain = level / peak;

	for (c = 0; c < self->n_channels; ++c) {
		const double chan_avg = average[c];
		float * const chan_data = self->data[c];
		const float * const end = &chan_data[self->length];
		float *it;

		for (it = chan_data; it != end; ++it) {
			*it -= chan_avg;
			*it *= gain;
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(Fragment_amp_doc,
"amp(gain)\n"
"\n"
"Amplify the fragment by the given ``gain`` in dB which can either be a float "
"value to apply to all channels or a tuple with a value for each channel.\n");

static PyObject *Fragment_amp(Fragment *self, PyObject *args)
{
	PyObject *gain_obj;

	double gain[MAX_CHANNELS];
	size_t c;

	if (!PyArg_ParseTuple(args, "O", &gain_obj))
		return NULL;

	if (PyFloat_Check(gain_obj)) {
		const double gain_lin = dB2lin(PyFloat_AsDouble(gain_obj));
		for (c = 0; c < self->n_channels; ++c)
			gain[c] = gain_lin;
	} else if (PyTuple_Check(gain_obj)) {
		if (PyTuple_GET_SIZE(gain_obj) != self->n_channels) {
			PyErr_SetString(PyExc_ValueError, "channels mismatch");
			return NULL;
		}

		for (c = 0; c < self->n_channels; ++c) {
			PyObject *o = PyTuple_GET_ITEM(gain_obj, c);
			gain[c] = dB2lin(PyFloat_AsDouble(o));
		}
	} else {
		PyErr_SetString(PyExc_TypeError, "invalid gain values");
		return NULL;
	}

	for (c = 0; c < self->n_channels; ++c) {
		const double g = gain[c];
		size_t i;

		for (i = 0; i < self->length; ++i)
			self->data[c][i] *= g;
	}

	Py_RETURN_NONE;
}

static PyMethodDef Fragment_methods[] = {
	{ "_resize", (PyCFunction)Fragment_resize, METH_VARARGS,
	  "Resize the internal buffer" },
	{ "mix", (PyCFunction)Fragment_mix, METH_VARARGS,
	  Fragment_mix_doc },
	{ "import_bytes", (PyCFunction)Fragment_import_bytes, METH_VARARGS,
	  "Import data as raw bytes" },
	{ "as_bytes", (PyCFunction)Fragment_as_bytes, METH_VARARGS,
	  "Make a byte buffer with the data" },
	{ "normalize", (PyCFunction)Fragment_normalize, METH_VARARGS,
	  Fragment_normalize_doc },
	{ "amp", (PyCFunction)Fragment_amp, METH_VARARGS,
	  Fragment_amp_doc },
	{ NULL }
};

static PyTypeObject geomusic_FragmentType = {
	PyObject_HEAD_INIT(NULL)
	0,                                 /* ob_size */
	"_geomusic.Fragment",              /* tp_name */
	sizeof(Fragment),                  /* tp_basicsize */
	0,                                 /* tp_itemsize */
	(destructor)Fragment_dealloc,      /* tp_dealloc */
	0,                                 /* tp_print */
	0,                                 /* tp_getattr */
	0,                                 /* tp_setattr */
	0,                                 /* tp_compare */
	0,                                 /* tp_repr */
	0,                                 /* tp_as_number */
	&Fragment_as_sequence,             /* tp_as_sequence */
	0,                                 /* tp_as_mapping */
	0,                                 /* tp_hash  */
	0,                                 /* tp_call */
	0,                                 /* tp_str */
	0,                                 /* tp_getattro */
	0,                                 /* tp_setattro */
	0,                                 /* tp_as_buffer */
	BASE_TYPE_FLAGS,                   /* tp_flags */
	"Fragment of audio data",          /* tp_doc */
	0,                                 /* tp_traverse */
	0,                                 /* tp_clear */
	0,                                 /* tp_richcompare */
	0,                                 /* tp_weaklistoffset */
	0,                                 /* tp_iter */
	0,                                 /* tp_iternext */
	Fragment_methods,                  /* tp_methods */
	0,                                 /* tp_members */
	Fragment_getsetters,               /* tp_getset */
	0,                                 /* tp_base */
	0,                                 /* tp_dict */
	0,                                 /* tp_descr_get */
	0,                                 /* tp_descr_set */
	0,                                 /* tp_dictoffset */
	(initproc)Fragment_init,           /* tp_init */
	0,                                 /* tp_alloc */
	Fragment_new,                      /* tp_new */
};

/* ----------------------------------------------------------------------------
 * _geomusic methods
 */

PyDoc_STRVAR(geomusic_lin2dB_doc,
"lin2dB(value)\n"
"\n"
"Convert linear ``value`` to dB.\n");

static PyObject *geomusic_lin2dB(PyObject *self, PyObject *args)
{
	double level;

	if (!PyArg_ParseTuple(args, "d", &level))
		return NULL;

	return PyFloat_FromDouble(lin2dB(level));
}

PyDoc_STRVAR(geomusic_dB2lin_doc,
"dB2lin(value)\n"
"\n"
"Convert dB ``value`` to linear.\n");

static PyObject *geomusic_dB2lin(PyObject *self, PyObject *args)
{
	double dB;

	if (!PyArg_ParseTuple(args, "d", &dB))
		return NULL;

	return PyFloat_FromDouble(dB2lin(dB));
}

PyDoc_STRVAR(geomusic_sine_doc,
"sine(fragment, frequency, levels)\n"
"\n"
"Generate a sine wave with constant ``levels`` at the given ``frequency`` "
"into the entirety of the provided ``fragment``.\n");

static PyObject *geomusic_sine(PyObject *self, PyObject *args)
{
	Fragment *frag;
	double freq;
	PyObject *levels_tuple;

	Py_ssize_t n_channels;
	double levels[MAX_CHANNELS];
	Py_ssize_t c, i;
	double k;

	if (!PyArg_ParseTuple(args, "O!dO!", &geomusic_FragmentType, &frag,
			      &freq, &PyTuple_Type, &levels_tuple))
		return NULL;

	n_channels = PyTuple_Size(levels_tuple);

	if (n_channels > MAX_CHANNELS) {
		PyErr_SetString(PyExc_ValueError, "too many channels");
		return NULL;
	}

	if (n_channels != frag->n_channels) {
		PyErr_SetString(PyExc_ValueError, "channels number mismatch");
		return NULL;
	}

	for (c = 0; c < n_channels; ++c) {
		PyObject *level = PyTuple_GetItem(levels_tuple, c);
		levels[c] = dB2lin(PyFloat_AsDouble(level));
	}

	k = 2 * M_PI * freq / frag->rate;

	for (i = 0; i < frag->length; ++i) {
		const double s = sin(k * i);

		for (c = 0; c < n_channels; ++c)
			frag->data[c][i] = s * levels[c];
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(geomusic_overtones_doc,
"overtones(fragment, frequency, levels, overtones)\n"
"\n"
"Generate a sum of overtones as pure sine waves with the given fundamental "
"``frequency`` and ``levels`` in dB.\n"
"\n"
"The ``overtones`` are described with a dictionary which keys are "
"floating point numbers multiplied by the fundamental frequency to get the "
"overtone frequencies, and values are tuples with levels for each channel "
"of the fragment.  The generation is performed over all of the fragment "
"data.\n");

static PyObject *geomusic_overtones(PyObject *self, PyObject *args)
{
	struct overtone {
		double freq;
		double levels[MAX_CHANNELS];
	};

	Fragment *frag;
	double freq;
	PyObject *levels_obj;
	PyObject *overtones_obj;

	struct overtone *overtones;
	struct overtone *ot;
	const struct overtone *ot_end;
	Py_ssize_t n_overtones;
	PyObject *ot_freq;
	PyObject *ot_levels;
	double levels[MAX_CHANNELS];
	Py_ssize_t pos;
	size_t i;
	size_t c;
	double k;
	int stat = 0;

	if (!PyArg_ParseTuple(args, "O!dO!O!", &geomusic_FragmentType, &frag,
			      &freq, &PyTuple_Type, &levels_obj,
			      &PyDict_Type, &overtones_obj))
		return NULL;

	if (PyTuple_GET_SIZE(levels_obj) != frag->n_channels) {
		PyErr_SetString(PyExc_ValueError, "channels number mismatch");
		return NULL;
	}

	for (i = 0; i < frag->n_channels; ++i) {
		PyObject *l = PyTuple_GET_ITEM(levels_obj, i);
		levels[i] = dB2lin(PyFloat_AsDouble(l));
	}

	n_overtones = PyDict_Size(overtones_obj);
	overtones = PyMem_Malloc(n_overtones * sizeof(struct overtone));

	if (overtones == NULL)
		return PyErr_NoMemory();

	ot = overtones;
	ot_end = &overtones[n_overtones];
	pos = 0;

	while (PyDict_Next(overtones_obj, &pos, &ot_freq, &ot_levels)) {
		if (!PyFloat_Check(ot_freq)) {
			PyErr_SetString(PyExc_TypeError,
					"overtone key must be a float");
			stat = -1;
			goto free_overtones;
		}

		if (!PyTuple_Check(ot_levels)) {
			PyErr_SetString(PyExc_TypeError,
					"overtone levels must be a tuple");
			stat = -1;
			goto free_overtones;
		}

		if (PyTuple_GET_SIZE(ot_levels) != frag->n_channels) {
			PyErr_SetString(PyExc_ValueError,
					"channels number mismatch");
			stat = -1;
			goto free_overtones;
		}

		ot->freq = PyFloat_AS_DOUBLE(ot_freq);

		for (c = 0; c < frag->n_channels; ++c) {
			PyObject *l = PyTuple_GET_ITEM(ot_levels, c);

			if (!PyFloat_Check(l)) {
				PyErr_SetString(
					PyExc_TypeError,
					"overtone level must be a float");
				stat = -1;
				goto free_overtones;
			}

			ot->levels[c] =
				dB2lin(PyFloat_AS_DOUBLE(l)) * levels[c];
		}

		++ot;
	}

	k = 2 * M_PI * freq / frag->rate;

	/* Silence harmonics above rate / 2 to avoid spectrum overlap */
	for (ot = overtones; ot != ot_end; ++ot)
		if ((ot->freq * freq) >= (frag->rate / 2))
			for (c = 0; c < frag->n_channels; ++c)
				ot->levels[c] = 0.0f;

	for (i = 0; i < frag->length; ++i) {
		const double m = k * i;

		for (ot = overtones; ot != ot_end; ++ot) {
			const double s = sin(m * ot->freq);

			for (c = 0; c < frag->n_channels; ++c)
				frag->data[c][i] += s * ot->levels[c];
		}
	}

free_overtones:
	PyMem_Free(overtones);

	if (stat < 0)
		return NULL;

	Py_RETURN_NONE;
}

PyDoc_STRVAR(geomusic_dec_envelope_doc,
"dec_envelope(frag, k=1.0, p=1.0)\n"
"\n"
"This filter applies a decreasing envelope with ``k`` and ``p`` arguments "
"as follows, for a sound signal ``s`` at index ``i``:\n"
"\n"
".. math::\n"
"\n"
"   s[i] = \\frac{s[i]}{(1 + \\frac{i}{k})^p}\n"
"\n");

static PyObject *geomusic_dec_envelope(PyObject *self, PyObject *args)
{
	Fragment *frag;
	double k = 1.0;
	double p = 1.0;

	size_t c;

	if (!PyArg_ParseTuple(args, "O!|dd", &geomusic_FragmentType, &frag,
			      &k, &p))
		return NULL;

	if (k == 0.0) {
		PyErr_SetString(PyExc_ValueError, "k must not be 0");
		return NULL;
	}

	for (c = 0; c < frag->n_channels; ++c) {
		size_t i;

		for (i = 0; i < frag->length; ++i) {
			const double m = pow(1.0 + ((double)i / k), p);
			frag->data[c][i] /= m;
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(geomusic_reverse_doc,
"reverse(frag)\n"
"\n"
"Reverse the order of all the samples.\n");

static PyObject *geomusic_reverse(PyObject *self, PyObject *args)
{
	Fragment *frag;

	size_t c;

	if (!PyArg_ParseTuple(args, "O!", &geomusic_FragmentType, &frag))
		return NULL;

	for (c = 0; c < frag->n_channels; ++c) {
		size_t i;
		size_t j;

		for (i = 0, j = (frag->length - 1); i < j; ++i, --j) {
			const double s = frag->data[c][i];

			frag->data[c][i] = frag->data[c][j];
			frag->data[c][j] = s;
		}
	}

	Py_RETURN_NONE;
}

PyDoc_STRVAR(geomusic_reverb_doc,
"reverb(frag, delays, time_factor=0.2, gain_factor=6.0, seed=0)\n"
"\n"
"This filter creates a fast basic reverb effect with some randomness.\n"
"\n"
"The ``delays`` are a list of 2-tuples with a delay duration in seconds and "
"a gain in dB.  They are used to repeat and mix the whole fragment once for "
"each element of the list, shifted by the given time and amplified by the "
"given gain.  All values must be floating point numbers.  The time delay must "
"not be negative - it's a *causal* reverb.\n"
"\n"
"The ``time_factor`` and ``gain_factor`` parameters are used when adding a "
"random element to the delay and gain.  For example, a ``time_factor`` of 0.2 "
"means the delay will be randomly picked between 1.0 and 1.2 times the value "
"given in the ``delays`` list.  Similarly, for a ``gain_factor`` of 6.0dB the "
"gain will be randomly picked within +/- 6dB around the given value in "
"``delays``.\n"
"\n"
"The ``seed`` argument can be used to initialise the pseudo-random number "
"sequence.  With the default value of 0, the seed will be initialised based "
"on the current time.\n"
"\n"
".. note::\n"
"\n"
"   This filter function can also produce a *delay* effect by specifiying "
"   only a few regularly spaced ``delays``.\n");

static PyObject *geomusic_reverb(PyObject *self, PyObject *args)
{
	struct delay {
		size_t time;
		double gain;
	};

	Fragment *frag;
	PyObject *delays_list;
	double time_factor = 0.2;
	double gain_factor = 6.0;
	unsigned int seed = 0;

	struct delay *delays[MAX_CHANNELS];
	Py_ssize_t n_delays;
	size_t max_delay;
	size_t max_index;
	size_t d;
	size_t c;
	size_t i;

	if (!PyArg_ParseTuple(args, "O!O!|ddI", &geomusic_FragmentType, &frag,
			      &PyList_Type, &delays_list, &time_factor,
			      &gain_factor, &seed))
		return NULL;

	if (!seed)
		seed = time(0);

	srand(seed);

	n_delays = PyList_GET_SIZE(delays_list);

	for (c = 0; c < frag->n_channels; ++c) {
		delays[c] = PyMem_Malloc(n_delays * sizeof(struct delay));

		if (delays[c] == NULL) {
			while (--c)
				PyMem_Free(delays[c]);

			return PyErr_NoMemory();
		}
	}

	max_delay = 0;

	for (d = 0; d < n_delays; ++d) {
		PyObject *pair = PyList_GetItem(delays_list, d);
		double time;
		double gain;

		if (!PyTuple_Check(pair)) {
			PyErr_SetString(PyExc_TypeError,
					"delay values must be a tuple");
			return NULL;
		}

		if (PyTuple_GET_SIZE(pair) != 2) {
			PyErr_SetString(PyExc_ValueError,
					"delay tuple length must be 2");
			return NULL;
		}

		time = PyFloat_AsDouble(PyTuple_GetItem(pair, 0));

		if (time < 0.0) {
			PyErr_SetString(PyExc_ValueError,
					"delay time must be >= 0");
			return NULL;
		}

		for (c = 0; c < frag->n_channels; ++c) {
			const double c_time =
				(time
				 * (1.0 + (rand() * time_factor / RAND_MAX)));
			delays[c][d].time = c_time * frag->rate;

			if (delays[c][d].time > max_delay)
				max_delay = delays[c][d].time;
		}

		gain = PyFloat_AsDouble(PyTuple_GetItem(pair, 1));

		for (c = 0; c < frag->n_channels; ++c) {
			const double c_gain =
				(gain - gain_factor
				 + (rand() * gain_factor * 2.0 / RAND_MAX));
			delays[c][d].gain = dB2lin(c_gain);
		}
	}

	max_index = frag->length - 1;

	if (do_resize(frag, (frag->length + max_delay)) < 0)
		return NULL;

	for (c = 0; c < frag->n_channels; ++c) {
		const struct delay *c_delay = delays[c];
		float *c_data = frag->data[c];

		i = max_index;

		do {
			const double s = c_data[i];

			for (d = 0; d < n_delays; ++d) {
				const float z = s * c_delay[d].gain;
				c_data[i + c_delay[d].time] += z;
			}
		} while (i--);
	}

	for (c = 0; c < frag->n_channels; ++c)
		PyMem_Free(delays[c]);

	Py_RETURN_NONE;
}

static PyMethodDef geomusic_methods[] = {
	{ "lin2dB", geomusic_lin2dB, METH_VARARGS,
	  geomusic_lin2dB_doc },
	{ "dB2lin", geomusic_dB2lin, METH_VARARGS,
	  geomusic_dB2lin_doc },
	{ "sine", geomusic_sine, METH_VARARGS,
	  geomusic_sine_doc },
	{ "overtones", geomusic_overtones, METH_VARARGS,
	  geomusic_overtones_doc },
	{ "dec_envelope", geomusic_dec_envelope, METH_VARARGS,
	  geomusic_dec_envelope_doc },
	{ "reverse", geomusic_reverse, METH_VARARGS,
	  geomusic_reverse_doc },
	{ "reverb", geomusic_reverb, METH_VARARGS,
	  geomusic_reverb_doc },
	{ NULL, NULL, 0, NULL }
};

PyMODINIT_FUNC init_geomusic(void)
{
	struct geomusic_type {
		PyTypeObject *type;
		const char *name;
	};
	static const struct geomusic_type geomusic_types[] = {
		{ &geomusic_FragmentType, "Fragment" },
		{ NULL, NULL }
	};
	const struct geomusic_type *it;
	PyObject *m;

	for (it = geomusic_types; it->type != NULL; ++it) {
		if (it->type->tp_new == NULL)
			it->type->tp_new = PyType_GenericNew;

		if (PyType_Ready(it->type))
			return;
	}

	m = Py_InitModule("_geomusic", geomusic_methods);

	for (it = geomusic_types; it->type != NULL; ++it) {
		Py_INCREF((PyObject *)it->type);
		PyModule_AddObject(m, it->name, (PyObject *)it->type);
	}
}
