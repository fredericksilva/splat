/*
    Splat - filter.c

    Copyright (C) 2015
    Guillaume Tucker <guillaume@mangoz.org>

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "_splat.h"

void splat_filter_dec_envelope(struct splat_fragment *frag, double k, double p)
{
	unsigned c;

	for (c = 0; c < frag->n_channels; ++c) {
		size_t i;

		for (i = 0; i < frag->length; ++i) {
			const double m = pow(1.0 + ((double)i / k), p);
			frag->data[c][i] /= m;
		}
	}
}

void splat_filter_reverse(struct splat_fragment *frag)
{
	unsigned c;

	for (c = 0; c < frag->n_channels; ++c) {
		size_t i;
		size_t j;

		for (i = 0, j = (frag->length - 1); i < j; ++i, --j) {
			const double s = frag->data[c][i];

			frag->data[c][i] = frag->data[c][j];
			frag->data[c][j] = s;
		}
	}
}

void splat_filter_reverb(struct splat_fragment *frag,
			 struct splat_delay **delays, size_t n_delays,
			 size_t max_index)
{
	size_t d;
	unsigned c;
	size_t i;

	for (c = 0; c < frag->n_channels; ++c) {
		const struct splat_delay *c_delay = delays[c];
		sample_t *c_data = frag->data[c];

		i = max_index;

		do {
			const double s = c_data[i];

			for (d = 0; d < n_delays; ++d) {
				const double z = s * c_delay[d].gain;

				c_data[i + c_delay[d].time] += z;
			}
		} while (i--);
	}
}
