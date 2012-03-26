#include <Python.h>
#include <math.h>

#define BASE_TYPE_FLAGS (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE)
#define MAX_CHANNELS 16

/* ----------------------------------------------------------------------------
 * Fragment implementation
 */

struct Fragment_object {
	PyObject_HEAD;
	unsigned n_channels;
	unsigned rate;
	Py_ssize_t length;
	float **data;
	long sample_size;
};
typedef struct Fragment_object Fragment;

static PyTypeObject geomusic_FragmentType;

static void Fragment_dealloc(Fragment *self)
{
	if (self->data) {
		unsigned i;

		for (i = 0; i < self->n_channels; ++i)
			free(self->data[i]);

		free(self->data);
		self->data = NULL;
	}

	self->ob_type->tp_free((PyObject *)self);
}

static int Fragment_init(Fragment *self, PyObject *args, PyObject *kw)
{
	unsigned n_channels;
	unsigned rate;
	float duration;

	unsigned i;
	size_t length;
	size_t data_size;

	if (!PyArg_ParseTuple(args, "IIf", &n_channels, &rate, &duration))
		return -1;

	if (duration < 0.0) {
		/* invalid arguments */
		return -1;
	}

	self->data = malloc(n_channels * sizeof(float *));

	if (!self->data) {
		/* memory error */
		return -1;
	}

	length = duration * rate;
	data_size = length * sizeof(float);

	for (i = 0; i < n_channels; ++i) {
		self->data[i] = malloc(data_size);

		if (!self->data[i]) {
			/* memory error */
			return -1;
		}

		memset(self->data[i], 0, data_size);
	}

	self->n_channels = n_channels;
	self->rate = rate;
	self->length = length;

	return 0;
}

static PyObject *Fragment_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
	Fragment *self;

	self = (Fragment *)type->tp_alloc(type, 0);

	if (!self) {
		/* memory error */
		return NULL;
	}

	self->data = NULL;
	self->sample_size = 0;

	return (PyObject *)self;
}

/* sequence interface */

static Py_ssize_t Fragment_sq_length(Fragment *self)
{
	return self->length;
}

static PyObject *Fragment_sq_item(Fragment *self, Py_ssize_t i)
{
	PyObject *sample;
	unsigned c;

	if (i >= self->length)
		return NULL;

	sample = PyTuple_New(self->n_channels);

	if (!sample) {
		/* alloc error */
		return NULL;
	}

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
		fprintf(stderr, "not a tuple\n");
		return -1;
	}

	if (i >= self->length) {
		fprintf(stderr, "set index error\n");
		return -1;
	}

	for (c = 0; c < PyTuple_GET_SIZE(v); ++c) {
		PyObject *s = PyTuple_GET_ITEM(v, c);

		if (!PyFloat_CheckExact(s)) {
			fprintf(stderr, "not a float\n");
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

/* getsetters */

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

/* methods */

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
		self->data[i] = realloc(self->data[i], data_size);

		if (!self->data[i]) {
			/* memory error */
			return -1;
		}

		memset(&self->data[i][start], 0, (data_size - start));
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

PyObject *Fragment_mix(Fragment *self, PyObject *args)
{
	Fragment *frag;
	float start;

	size_t start_sample;
	size_t total_length;
	Py_ssize_t c;

	if (!PyArg_ParseTuple(args, "O!f", &geomusic_FragmentType, &frag,
			      &start))
		return NULL;

	if (frag->n_channels != self->n_channels) {
		fprintf(stderr, "channels mismatch\n");
		return NULL;
	}

	start_sample = start * self->rate;
	total_length = start_sample + frag->length;

	if (do_resize(self, total_length) < 0)
		return NULL;

	for (c = 0; c < self->n_channels; ++c) {
		const float *src = frag->data[c];
		float *dst =  &self->data[c][start_sample];
		size_t i = frag->length;

		while (i--)
			*dst++ += *src++;
	}

	Py_RETURN_NONE;
}

static PyMethodDef Fragment_methods[] = {
	{ "_resize", (PyCFunction)Fragment_resize, METH_VARARGS,
	  "Resize the internal buffer" },
	{ "mix", (PyCFunction)Fragment_mix, METH_VARARGS,
	  "Mix two fragments together" },
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
	0, /*&Fragment_as_buffer,*/               /* tp_as_buffer */
	BASE_TYPE_FLAGS,                   /* tp_flags */
	"Geomusic Fragment",               /* tp_doc */
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
 * Sine wave
 */

static PyObject *geomusic_sine(PyObject *self, PyObject *args)
{
	Fragment *frag;
	float freq;
	PyObject *levels_tuple;

	Py_ssize_t n_channels;
	float levels[MAX_CHANNELS];
	Py_ssize_t c, i;
	float k;

	if (!PyArg_ParseTuple(args, "O!fO!", &geomusic_FragmentType, &frag,
			      &freq, &PyTuple_Type, &levels_tuple))
		return NULL;

	n_channels = PyTuple_Size(levels_tuple);

	if (n_channels > MAX_CHANNELS) {
		fprintf(stderr, "Too many channels: %zi\n", n_channels);
		return NULL;
	}

	if (n_channels != frag->n_channels) {
		fprintf(stderr, "Channels mismatch\n");
		return NULL;
	}

	/* ToDo: levels in dB */
	for (c = 0; c < n_channels; ++c) {
		PyObject *level = PyTuple_GetItem(levels_tuple, c);
		levels[c] = (float)PyFloat_AsDouble(level);
	}

	k = 2 * M_PI * freq / frag->rate;

	for (i = 0; i < frag->length; ++i) {
		const float s = sin(k * i);

		for (c = 0; c < n_channels; ++c)
			frag->data[c][i] = s * levels[c];
	}

	Py_RETURN_NONE;
}

static PyMethodDef geomusic_methods[] = {
	{ "sine", geomusic_sine, METH_VARARGS, "Make a sine wave" },
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
