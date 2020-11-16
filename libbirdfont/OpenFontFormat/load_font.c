/*
	Copyright (C) 2013 2014 2015 2018 Johan Mattsson

	This library is free software; you can redistribute it and/or modify 
	it under the terms of the GNU Lesser General Public License as 
	published by the Free Software Foundation; either version 3 of the 
	License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful, but 
	WITHOUT ANY WARRANTY; without even the implied warranty of 
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
	Lesser General Public License for more details.
*/

#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OPENTYPE_VALIDATE_H
#include FT_TRUETYPE_TABLES_H
#include FT_SFNT_NAMES_H

#include "birdfont.h"

/** Error codes. */
#define OK 0

/** Point flags. */
#define QUADRATIC_OFF_CURVE 0
#define ON_CURVE 1
#define CUBIC_CURVE 2
#define DOUBLE_CURVE 4
#define HIDDEN_CURVE 8

typedef struct FontFace {
	FT_Face face;
	FT_Library library;
} FontFace;

void close_ft_font (FontFace* font);

/** Convert units per em in font file format to the BirdFont format. */
double get_units (double units_per_em) {
	return 100.0 / units_per_em;
}

/** @return a non zero value if the point is a part of two off curve 
 * points with a hidden on curve point in between.
 */
guint is_double_curve (char flag) {
	return flag & DOUBLE_CURVE;
}

/** @return a non zero value if the point is a cubic off curve point. */
guint is_cubic (char flag) {
	return flag & CUBIC_CURVE && (flag & ON_CURVE) == 0;
}

/** @return a non zero value if the point is a quadratic off curve point. */
guint is_quadratic (char flag) {
	return (flag & CUBIC_CURVE) == 0 && (flag & ON_CURVE) == 0;
}

/** @return a non zero value if the point is a on curve point */
guint is_line (char flag) {
	return flag & ON_CURVE;
}

/** @return a non zero value if the point is a hidden on curve point */
guint is_hidden (char flag) {
	return flag & HIDDEN_CURVE;
}

/** Convert every second hidden curve to a double curve and keep 
 * other points as on curve points.
 */ 
void set_double_curves (char* flag, int length) {
	int i;
	guint double_curve = FALSE;
	
	for (i = 1; i < length; i++) {
		if (is_line (flag[i])) {
			double_curve = FALSE;
		} else if (is_hidden (flag[i])) {
			if (!double_curve) {
				if (is_quadratic (flag[i - 1]) && is_quadratic (flag[i + 1])) {
					flag[i - 1] = DOUBLE_CURVE;
					flag[i] = HIDDEN_CURVE;
					flag[i + 1] = DOUBLE_CURVE;
					double_curve = TRUE;
				} else {
					flag[i] = ON_CURVE;
					double_curve = FALSE;
				}
			} else {
				flag[i] = ON_CURVE;
				double_curve = FALSE;
			} 
		}
		// ignore off curve points
	}
}

/** @return half the distance between two points. */
double half_way (double prev, double current) {
	return prev + ((current - prev) / 2.0);
} 

/** Remove hidden points.
 * @return length after removal
 */
int remove_hidden_points (FT_Vector* points, char* flags, guint length, guint capacity) {
	int k;
	int l;
	
	l = 0;
	for (k = 0; k < length; k++) {
		if (!is_hidden (flags[k])) {
			points[l] = points[k];
			flags[l] = flags[k];
			l++;
		}
	}
	
	for (k = l; k < capacity; k++) {
		points[l].x = 0;
		points[l].y = 0;
		flags[l] = 0;	
	}

	return l;
}	

/** Add extra point where two ore more off curve points follow each other. */
void create_contour (guint unicode, FT_Vector* points, char* flags, int* length, FT_Vector** new_points, char** new_flags, int* err) {
	// This function has been tested with many fonts. ElsieSwashCaps-Regular
	// is one of the more interesting fonts I have seen.
	int i;
	int j;
	guint prev_is_curve;
	guint first_is_curve = FALSE;
	guint first_normal_off_curve = FALSE;
	double x = 0;
	double y = 0;
	FT_Vector* p;
	char* f;
	int len = *length;
	
	*new_points = malloc (4 * len * sizeof (FT_Vector));
	*new_flags = malloc (4 * len * sizeof (char));
	
	p = *new_points;
	f = *new_flags;
	
	for (i = 0; i < 4 * len; i++) {
		p[i].x = 0;
		p[i].y = 0;
		f[i] = 0;
	}
	
	if (len == 0) {
		return;
	}
	
	prev_is_curve = is_quadratic (flags[len - 1]);

	j = 0;
	i = 0;
	
	if (len > 2 && is_quadratic (flags[0]) && is_quadratic (flags[1])) {
		p[j].x = half_way (points[0].x, points[1].x);
		p[j].y = half_way (points[0].y, points[1].y);
						
		f[j] = ON_CURVE;
		prev_is_curve = FALSE;
		first_is_curve = TRUE;
		j++;
		i++;		
	} 
	
	// first point is of curve but it is not part of a double curve
	if (len > 2 && is_quadratic (flags[0]) && !is_quadratic (flags[1])) { 
		first_normal_off_curve = TRUE;
	}
	
	while (i < len) {
		if (is_quadratic (flags[i])) {
			if (prev_is_curve && j != 0) {
				// add a new point if half way between two off curve points
				x = half_way (points[i].x, p[j - 1].x);
				y = half_way (points[i].y, p[j - 1].y);

				p[j].x = x;
				p[j].y = y;
				
				f[j] = HIDDEN_CURVE;

				j++;
			}
			
			if (i == 0) { // A fix for FiraCode Regular
				i++; 
				continue;
			}
			
			f[j] = QUADRATIC_OFF_CURVE;
			prev_is_curve = TRUE;
		} else if (is_line (flags[i])) {
			prev_is_curve = FALSE;
			f[j] = ON_CURVE;
		} else if (is_cubic (flags[i])) {
			prev_is_curve = FALSE;
			f[j] = CUBIC_CURVE;
		} else {
			g_warning ("WARNING invalid point flags: %d index: %d.\n", flags[i], i);
			prev_is_curve = FALSE;
			f[j] = ON_CURVE;
		}
		
		p[j] = points[i];
		j++;
		i++;
	}

	// last to first point
	if (first_is_curve && !prev_is_curve && is_quadratic (flags[i])) {
		p[j] = points[i];
		f[j] = QUADRATIC_OFF_CURVE;
		j++;
		i++;
		
		p[j].x = half_way (p[j - 1].x, points[0].x);
		p[j].y = half_way (p[j - 1].y, points[0].y);					
		f[j] = ON_CURVE; // FIXME
		prev_is_curve = FALSE;
		j++;
		i++;

		p[j] = points[0];
		f[j] = QUADRATIC_OFF_CURVE;
		j++;
		i++;
						
		p[j] = p[0];
		f[j] = f[0];
		j++;
	} else if (first_is_curve && !prev_is_curve && is_line (flags[i])) {
		p[j] = points[i];
		f[j] = ON_CURVE;
		j++;
		i++;	

		p[j] = points[0];
		f[j] = QUADRATIC_OFF_CURVE;
		j++;
		i++;
		
		p[j] = p[0];
		f[j] = f[0];
		j++;
	} else if (first_is_curve && prev_is_curve && is_quadratic (flags[i])) {
		x = half_way (p[j - 1].x, points[i].x);
		y = half_way (p[j - 1].y, points[i].y);
		p[j].x = x;
		p[j].y = y;
		f[j] = HIDDEN_CURVE;
		j++;

		p[j] = points[i];
		f[j] = flags[i];
		j++;
		i++;

		p[j].x = half_way (p[j - 1].x, points[0].x);
		p[j].y = half_way (p[j - 1].y, points[0].y);					
		f[j] = HIDDEN_CURVE;
		prev_is_curve = FALSE;
		
		j++;
		i++;

		p[j] = points[0];
		f[j] = QUADRATIC_OFF_CURVE;
		j++;
		
		p[j] = p[0];
		f[j] = ON_CURVE;
		j++;

		prev_is_curve = TRUE;
	} else if (prev_is_curve && (flags[0] & ON_CURVE) == 0) {
		if (is_quadratic (f[j - 1]) && is_quadratic (flags[i])) {
			x = half_way (p[j - 1].x, points[i].x); 
			y = half_way (p[j - 1].y, points[i].y);
			p[j].x = x;
			p[j].y = y;
			f[j] = HIDDEN_CURVE;
			j++;
		}

		p[j] = points[i];
		if (is_line (flags[i])) {
			f[j] = ON_CURVE;
		} else {
			f[j] = QUADRATIC_OFF_CURVE;
		}
		j++;		

		if (is_quadratic (f[0]) && is_quadratic (flags[0])) {
			x = half_way (p[j - 1].x, points[0].x);
			y = half_way (p[j - 1].y, points[0].y);
			p[j].x = x;
			p[j].y = y;
			f[j] = HIDDEN_CURVE;
			j++;
		}
		
		x = points[0].x;
		y = points[0].y;

		p[j].x = x;
		p[j].y = y;

		f[j] = QUADRATIC_OFF_CURVE;
		j++;		
	} else if (prev_is_curve && is_quadratic (flags[i])) {
		x = p[j - 1].x + ((points[i].x - p[j - 1].x) / 2.0);
		y = p[j - 1].y + ((points[i].y - p[j - 1].y) / 2.0);
		p[j].x = x;
		p[j].y = y;
		f[j] = HIDDEN_CURVE;
		j++;

		p[j] = points[i];
		f[j] = QUADRATIC_OFF_CURVE;
		j++;
		i++;

		if (is_quadratic (f[0])) {
			x = half_way (p[j - 1].x, points[i].x);
			y = half_way (p[j - 1].y, points[i].y);
			p[j].x = x;
			p[j].y = y;
			f[j] = HIDDEN_CURVE;
			j++;
			
			p[j] = p[0];
			f[j] = f[0];
			j++;
		} else { 
			p[j] = p[0];
			f[j] = f[0];
			j++;
		}
		
		prev_is_curve = TRUE;
	} else {
		p[j] = points[i];
		if (is_quadratic (flags[i])) 
			f[j] = QUADRATIC_OFF_CURVE;
		else 
			f[j] = ON_CURVE;
		j++;

		p[j] = p[0];
		if (is_quadratic (flags[i])) 
			f[j] = QUADRATIC_OFF_CURVE;
		else 
			f[j] = ON_CURVE;
		j++;
	}

	set_double_curves (f, j);
	*length = remove_hidden_points (p, f, j, 2 * len);
}

/** Get path data in .bf format. See BirdFontFile.get_point_data () for 
 * format specification.
 */
GString* get_bf_contour_data (guint unicode, FT_Vector* points, char* flags, int length, double units_per_em, int* err) {
	GString* bf = g_string_new ("");
	GString* contour;
	int i = 0;
	FT_Vector* new_points;
	char* new_flags;
	double x0, x1, x2;
	double y0, y1, y2;
	gchar coordinate_buffer[80];
	gchar* coordinate = (gchar*) &coordinate_buffer;
	double units = get_units (units_per_em);
	guint prev_is_curve;
	int points_length;

	if (length == 0) {
		return bf;
	}
	
	points_length = length;
	create_contour (unicode, points, flags, &length, &new_points, &new_flags, err);
	
	x0 = new_points[0].x * units;
	y0 = new_points[0].y * units;
	
	g_string_printf (bf, "S ");
	
	g_ascii_formatd (coordinate, 80, "%f", x0);
	g_string_append (bf, coordinate);
	g_string_append (bf, ",");

	g_ascii_formatd (coordinate, 80, "%f", y0);
	g_string_append (bf, coordinate);

	i = 1;
	while (i < length) {
		contour = g_string_new ("");
		
		if (is_hidden (new_flags[i])) {
			// Skip it
			g_string_append (contour, "");
			i++;
		} else if (is_cubic (new_flags[i])) {
			x0 = new_points[i].x * units;
			y0 = new_points[i].y * units;
			x1 = new_points[i+1].x * units;
			y1 = new_points[i+1].y * units;
			x2 = new_points[i+2].x * units;
			y2 = new_points[i+2].y * units;
			
			g_string_printf (contour, " C ");
			
			g_ascii_formatd (coordinate, 80, "%f", x0);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y0);
			g_string_append (contour, coordinate);
			g_string_append (contour, " ");

			g_ascii_formatd (coordinate, 80, "%f", x1);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y1);
			g_string_append (contour, coordinate);
			g_string_append (contour, " ");

			g_ascii_formatd (coordinate, 80, "%f", x2);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y2);
			g_string_append (contour, coordinate);
			
			i += 3;
		} else if (is_double_curve (new_flags[i])) {
			x0 = new_points[i].x * units;
			y0 = new_points[i].y * units;
			x1 = new_points[i+1].x * units;
			y1 = new_points[i+1].y * units;
			x2 = new_points[i+2].x * units;
			y2 = new_points[i+2].y * units;

			g_string_printf (contour, " D ");
			
			g_ascii_formatd (coordinate, 80, "%f", x0);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y0);
			g_string_append (contour, coordinate);
			g_string_append (contour, " ");

			g_ascii_formatd (coordinate, 80, "%f", x1);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y1);
			g_string_append (contour, coordinate);
			g_string_append (contour, " ");

			g_ascii_formatd (coordinate, 80, "%f", x2);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y2);
			g_string_append (contour, coordinate);
			
			i += 3;
		} else if (is_quadratic (new_flags[i])) {
			x0 = new_points[i].x * units;
			y0 = new_points[i].y * units;
			x1 = new_points[i+1].x * units;
			y1 = new_points[i+1].y * units;

			g_string_printf (contour, " Q ");
			
			g_ascii_formatd (coordinate, 80, "%f", x0);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y0);
			g_string_append (contour, coordinate);
			g_string_append (contour, " ");

			g_ascii_formatd (coordinate, 80, "%f", x1);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y1);
			g_string_append (contour, coordinate);
			
			i += 2;
		} else if (is_line (new_flags[i])) {
			x0 = new_points[i].x * units;
			y0 = new_points[i].y * units;

			g_string_printf (contour, " L ");
			
			g_ascii_formatd (coordinate, 80, "%f", x0);
			g_string_append (contour, coordinate);
			g_string_append (contour, ",");

			g_ascii_formatd (coordinate, 80, "%f", y0);
			g_string_append (contour, coordinate);
			
			i += 1;
		} else {
			contour = g_string_new ("");
			g_warning ("WARNING Can't parse outline.\n");
			*err = 1;
			i++;
		}
		
		g_string_append (bf, contour->str);
		g_string_free (contour, TRUE);
	}
	
	free (new_points);
	free (new_flags);
	
	return bf;
}

/** Get path element in .bf format. */
GString* get_bf_path (guint unicode, FT_Face face, double units_per_em, int* err) {
	GString* bf = g_string_new ("");
	GString* contour;
	FT_Error error;
	int i;
	int start;
	int end;

	if (face->glyph->outline.n_points == 0) {
		return bf;			
	}

	start = 0;
	for (i = 0; i < face->glyph->outline.n_contours; i++) {
		end = face->glyph->outline.contours [i];
		contour = get_bf_contour_data (unicode, face->glyph->outline.points + start, face->glyph->outline.tags + start, end - start, units_per_em, err);
		g_string_append_printf (bf, "\t\t<path data=\"%s\" />\n", contour->str);
		g_string_free (contour, TRUE);
		start = face->glyph->outline.contours [i] + 1;
	}

	return bf;
}

FontFace* open_font (const char* file) {
	FT_Library library = NULL;
	FT_Face face = NULL;
	int error;
	FontFace* font;
	
	error = FT_Init_FreeType (&library);
	if (error != OK) {
		printf ("Freetype init error %d.\n", error);
		return NULL;
	}

	error = FT_New_Face (library, file, 0, &face);
	if (error) {
		if (FT_Done_FreeType (library) != 0) {
			g_warning ("Can't close freetype.");
		}
		
		g_warning ("Freetype font face error %d\n", error);
		return NULL;
	}
	
	font = malloc (sizeof (FontFace));
	font->face = face;
	font->library = library;
	
	error = FT_Select_Charmap (face , FT_ENCODING_UNICODE);
	if (error) {
		g_warning ("Freetype can not use Unicode, error: %d\n", error);
		close_ft_font (font);
		return NULL;
	}
	
	return font;
}

void close_ft_font (FontFace* font) {
	if (font != NULL) {
		if (font->face != NULL) {
			if (FT_Done_Face (font->face) != 0) {
				g_warning ("Can't close font.");
			}
			font->face = NULL;
		}
		
		if (font->library != NULL) {
			if (FT_Done_FreeType (font->library) != 0) {
				g_warning ("Can't close freetype.");
			}
			font->library = NULL;
		}
		
		free (font);
	}
}

GString* load_glyph (FontFace* font, guint unicode) {
	GString* glyph;
	GString* paths;
	int err = OK;
	int gid;
	FT_ULong charcode = (FT_ULong) unicode;
	double units;
	gchar line_position_buffer[80];
	gchar* line_position = (gchar*) &line_position_buffer;
	FT_Error error;
	
	if (font == NULL || font->face == NULL || font->library == NULL) {
		printf ("WARNING No font in load_glyph");
		return NULL;
	}
	
	gid = FT_Get_Char_Index (font->face, charcode);
	
	if (gid == 0) {
		return NULL;
	}
	
	units = get_units (font->face->units_per_EM);
	
	glyph = g_string_new ("<font>");

	g_string_append_printf (glyph, "<horizontal>\n");

	g_ascii_formatd (line_position, 80, "%f", font->face->ascender * units);
	g_string_append_printf (glyph, "\t<top_limit>%s</top_limit>\n", line_position);

	g_string_append_printf (glyph, "\t<base_line>0</base_line>\n");
	
	g_ascii_formatd (line_position, 80, "%f", font->face->descender * units);
	g_string_append_printf (glyph, "\t<bottom_limit>%s</bottom_limit>\n", line_position);		
	
	g_string_append_printf (glyph, "</horizontal>\n");

	error = FT_Load_Glyph(font->face, gid, FT_LOAD_DEFAULT | FT_LOAD_NO_SCALE);
	
	if (error != 0) {
		printf ("WARNING Failed to load glyph.");
		g_string_free (glyph, TRUE);
		return NULL;
	}
	
	paths = get_bf_path (unicode, font->face, font->face->units_per_EM, &err);
	
	if (err != OK) {
		printf ("WARNING Can't load glyph.");
		g_string_free (glyph, TRUE);
		g_string_free (paths, TRUE);
		return NULL;
	}

	g_string_append_printf (glyph, "<collection unicode=\"U+%x\">\n", (guint)charcode);
	g_string_append_printf (glyph, "\t<selected id=\"0\" />\n");
	g_string_append_printf (glyph, "\t<glyph id=\"0\" left=\"%f\" right=\"%f\">\n", 
		0.0, font->face->glyph->metrics.horiAdvance * units);
		
	g_string_append_printf (glyph, "%s", paths->str);
	g_string_append_printf (glyph, "%s", "\t</glyph>");
	g_string_append_printf (glyph, "%s", "\t</collection>");
	g_string_append_printf (glyph, "%s", "</font>");
	
	g_string_free (paths, TRUE);
	
	if (err != OK) {
		g_warning ("Can't load glyph data.");
	}
	
	return glyph;
}

/** Get all character codes for a glyph.
 * @gid glyph id
 * @return array of character codes
 */
FT_ULong* get_charcodes (FT_Face face, FT_UInt gid) {
	FT_ULong charcode = 0;
	FT_UInt gindex;
	const int MAX = 255;
	FT_ULong* result = malloc (sizeof (FT_ULong) * (MAX + 1));
	guint results = 0;
	// TODO: find the lookup function in freetype
    
	charcode = FT_Get_First_Char (face, &gindex);

	while (gindex != 0) {

		if (results >= MAX) {
			g_warning ("Too many code points in font for one GID");
			break;
		}
		
		charcode = FT_Get_Next_Char (face, charcode, &gindex);
		if (gindex == gid && charcode != 0) {
			result[results] = charcode;
			results++;
		}
	}
	
	if (results == 0) {
		g_warning ("Can not find unicode value for gid %d.", gid);
	}
	
	result[results] = 0;
	results++;	
		
	return result;
}

/** Height of letter. */
int get_height (FT_Face face, guint unichar) {
	int error;
	FT_ULong charcode = (FT_ULong) unichar;
	FT_UInt index = FT_Get_Char_Index (face, charcode);

	error = FT_Load_Glyph (face, index, FT_LOAD_DEFAULT | FT_LOAD_NO_SCALE);
	if (error) {
		g_warning ("Failed to obtain height. (%d)\n", error);
		return 0;
	}
		
	return (int) face->glyph->metrics.height;	
}

/** Height of lower case letters. */
int get_xheight (FT_Face face) {
	return get_height (face, 'x');
}

/** Height of upper case letters. */
int get_top (FT_Face face) {
	return get_height (face, 'X');
}
/** Obtain descender. */
int get_descender (FT_Face face) {
	int error;
	FT_BBox bbox;
	FT_ULong charcode = (FT_ULong) 'g';
	FT_UInt index = FT_Get_Char_Index (face, charcode);
	FT_Glyph glyph;
	
	error = FT_Load_Glyph (face, index, FT_LOAD_DEFAULT | FT_LOAD_NO_SCALE);
	if (error) {
		g_warning ("Failed to obtain descender. (%d)\n", error);
		return 0;
	}
	
	FT_Get_Glyph (face->glyph, &glyph); 
	FT_Glyph_Get_CBox (glyph, FT_GLYPH_BBOX_UNSCALED, &bbox);
	
	return (int) bbox.yMin;	
}

/** Append name table data to a gstring. */
void append_description (GString* str, FT_SfntName* name_table_data) {
	gchar* utf8_str;
	gsize read, written;
	GError* error = NULL;	
	
	if (name_table_data->encoding_id == 0) { // mac roman
		utf8_str = g_convert (name_table_data->string, name_table_data->string_len, "utf-8", "macintosh", &read, &written, &error);

		if (error == NULL) {
			g_string_append (str, g_markup_escape_text (utf8_str, -1));
			g_free (utf8_str);
		} else {
			g_warning ("Error in append_description: %s\n", error->message);
			g_error_free (error);
		}
	} else if (name_table_data->encoding_id == 1) { // windows unicode
		utf8_str = g_convert (name_table_data->string, name_table_data->string_len, "utf-8", "ucs-2be", &read, &written, &error);

		if (error == NULL) {
			g_string_append (str, g_markup_escape_text (utf8_str, -1));
			g_free (utf8_str);
		} else {
			g_warning ("Error in append_description: %s\n", error->message);
			g_error_free (error);
		}
	} else {
		g_warning ("Encoding %u is not supported for platform %d.\n", name_table_data->encoding_id, name_table_data->platform_id);
	}
}

int getIndexForNameIdEncoding (FT_Face face, int id, int encoding) {
	FT_SfntName name_table_data;
	FT_Error error;
	int c = FT_Get_Sfnt_Name_Count (face);
	int i = 0;
	while (i < c){
		error = FT_Get_Sfnt_Name (face, i, &name_table_data);
		
		if (error == 0
			&& name_table_data.name_id == id 
			&& name_table_data.encoding_id == encoding) {
			return i;
		}
		
		i++;
	}
	
	return -1;
}

int getIndexForNameId (FT_Face face, int id) {
	int i = getIndexForNameIdEncoding (face, id, 1);
	
	if (i != -1) {
		return i;
		
	}
	
	return getIndexForNameIdEncoding (face, id, 0);
}

/** Convert font to bf format.
 * @param face freetype font face
 * @param err error code
 * @return xml representation of a bf font
 */
GString* get_bf_font (FT_Face face, char* file, int* err) {
	GString* bf = g_string_new ("");
	GString* bf_data;
	gchar* kerning;
	GString* glyph;
	FT_Error error;
	FT_Long i;
	FT_ULong charcode;
	FT_UInt gid;
	FT_SfntName name_table_data;
	double units_per_em;
	double units;
	gchar line_position_buffer[80];
	gchar* line_position = (gchar*) &line_position_buffer;
	int name_index;
	double gap;
	
	*err = OK;

	units_per_em = face->units_per_EM;
	units = get_units (units_per_em);
	
	if (face == NULL) {
		return  bf;
	}
	
	g_string_append (bf, "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n");
	g_string_append (bf, "<font>\n");

	g_string_append_printf (bf, "<postscript_name>%s</postscript_name>\n", g_markup_escape_text (FT_Get_Postscript_Name(face), -1));
	g_string_append_printf (bf, "<name>%s</name>\n", g_markup_escape_text (face->family_name, -1));
	
	if (face->style_name != NULL) {
		g_string_append_printf (bf, "<subfamily>%s</subfamily>\n", g_markup_escape_text (face->style_name, -1));
	} else {
		g_string_append_printf (bf, "<subfamily></subfamily>\n");
	}
	
	// full name
	name_index = getIndexForNameId (face, 4);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) { 
		g_string_append (bf, "<full_name>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</full_name>\n");
	} else {
		g_string_append (bf, "<full_name></full_name>\n");
	}

	// unique identifier
	name_index = getIndexForNameId (face, 3);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) { 
		g_string_append (bf, "<unique_identifier>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</unique_identifier>\n");
	} else {
		g_string_append (bf, "<unique_identifier></unique_identifier>\n");
	}

	// version
	name_index = getIndexForNameId (face, 5);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) {
		g_string_append (bf, "<version>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</version>\n");
	} else {
		g_string_append (bf, "<version></version>\n");
	}

	name_index = getIndexForNameId (face, 10);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) { // description
		g_string_append (bf, "<description>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</description>\n");
	} else {
		g_string_append (bf, "<description></description>\n");
	}
	
	// copyright
	name_index = getIndexForNameId (face, 0);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) { 
		g_string_append (bf, "<copyright>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</copyright>\n");
	} else {
		g_string_append (bf, "<copyright></copyright>\n");
	}

	name_index = getIndexForNameId (face, 7);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) {
		g_string_append (bf, "<trademark>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</trademark>\n");
	} else {
		g_string_append (bf, "<trademark></trademark>\n");
	}

	name_index = getIndexForNameId (face, 8);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, 8, &name_table_data) == 0) {
		g_string_append (bf, "<manefacturer>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</manefacturer>\n");
	} else {
		g_string_append (bf, "<manefacturer></manefacturer>\n");
	}

	name_index = getIndexForNameId (face, 9);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) {
		g_string_append (bf, "<designer>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</designer>\n");
	} else {
		g_string_append (bf, "<designer></designer>\n");
	}

	name_index = getIndexForNameId (face, 11);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) {
		g_string_append (bf, "<vendor_url>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</vendor_url>\n");
	} else {
		g_string_append (bf, "<vendor_url></vendor_url>\n");
	}

	name_index = getIndexForNameId (face, 12);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) {
		g_string_append (bf, "<designer_url>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</designer_url>\n");
	} else {
		g_string_append (bf, "<designer_url></designer_url>\n");
	}

	name_index = getIndexForNameId (face, 13);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) {
		g_string_append (bf, "<license>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</license>\n");
	} else {
		g_string_append (bf, "<license></license>\n");
	}
	
	name_index = getIndexForNameId (face, 14);
	if (name_index != -1 && FT_Get_Sfnt_Name (face, name_index, &name_table_data) == 0) {
		g_string_append (bf, "<license_url>");
		append_description (bf, &name_table_data);
		g_string_append (bf, "</license_url>\n");
	} else {
		g_string_append (bf, "<license_url></license_url>\n");
	}
		
	g_string_append_printf (bf, "<backup>%s</backup>\n", g_markup_escape_text (file, -1));


	g_string_append_printf (bf, "<horizontal>\n");
	
	g_ascii_formatd (line_position, 80, "%f", face->ascender * units);
	g_string_append_printf (bf, "\t<top_limit>%s</top_limit>\n", line_position);
	
	g_ascii_formatd (line_position, 80, "%f", get_top (face) * units);
	g_string_append_printf (bf, "\t<top_position>%s</top_position>\n", line_position);
	
	g_ascii_formatd (line_position, 80, "%f", get_xheight (face) * units);
	g_string_append_printf (bf, "\t<x-height>%s</x-height>\n", line_position);
	
	g_string_append_printf (bf, "\t<base_line>0</base_line>\n");
	
	g_ascii_formatd (line_position, 80, "%f", get_descender (face) * units);
	g_string_append_printf (bf, "\t<bottom_position>%s</bottom_position>\n", line_position);
	
	g_ascii_formatd (line_position, 80, "%f", face->descender * units);
	g_string_append_printf (bf, "\t<bottom_limit>%s</bottom_limit>\n", line_position);		
	
	gap = (face->height - (face->ascender - face->descender)) * units;
	g_ascii_formatd (line_position, 80, "%f", gap);
	g_string_append_printf (bf, "\t<line_gap>%s</line_gap>\n", line_position);		
	
	g_string_append_printf (bf, "</horizontal>\n");

	// space character
	gid = FT_Get_Char_Index (face, ' ');
	if (gid != 0) {
		FT_Load_Glyph(face, gid, FT_LOAD_DEFAULT | FT_LOAD_NO_SCALE);
		g_string_append_printf (bf, "<collection unicode=\"U+20\">\n");
		g_string_append_printf (bf, "\t<glyph left=\"%f\" right=\"%f\" selected=\"true\">\n", 0.0, face->glyph->metrics.horiAdvance * units);
		
		bf_data = get_bf_path (charcode, face, units_per_em, err);
		g_string_append (bf, bf_data->str);	
					
		g_string_append (bf, "\t</glyph>\n");
		g_string_append_printf (bf, "</collection>\n");
	}

	// glyph outlines
	for (i = 0; i < face->num_glyphs; i++) {
		error = FT_Load_Glyph (face, i, FT_LOAD_DEFAULT | FT_LOAD_NO_SCALE);
		if (error) {
			g_warning ("Freetype failed to load glyph %d.\n", (int)i);
			g_warning ("FT_Load_Glyph error %d\n", error);
			*err = 1;
			return bf;
		}

		if (face->glyph->format != ft_glyph_format_outline) {
			g_warning ("Freetype error no outline found in glyph.\n");
			*err = 1;
			return bf;
		}

		FT_ULong* charcodes = get_charcodes (face, i);
		int charcode_index = 0;
		
		while (TRUE) {	
			charcode = charcodes[charcode_index];
			
			if (charcode == 0) {
				free (charcodes);
				charcodes = NULL;
				break;
			}
			
			glyph = g_string_new ("");
			
			if (charcode > 32) { // not control character
				g_string_append_printf (glyph, "<collection unicode=\"U+%x\">\n", (guint)charcode);
				g_string_append_printf (glyph, "\t<glyph left=\"%f\" right=\"%f\" selected=\"true\">\n", 0.0, face->glyph->metrics.horiAdvance * units);
				
				bf_data = get_bf_path (charcode, face, units_per_em, err);
				g_string_append (glyph, bf_data->str);	
							
				g_string_append (glyph, "\t</glyph>\n");
				g_string_append_printf (glyph, "</collection>\n");
			} else {
				g_warning ("Ignoring control character, %d.", (guint)charcode);
			}

			g_string_append (bf, glyph->str);
			g_string_free (glyph, TRUE);
			
			charcode_index++;
		}
	}

	bird_font_open_font_format_reader_append_kerning (bf, file);

	g_string_append (bf, "</font>\n");
	
	return bf;
}

/** Load typeface with freetype2 and return the result as a bf font. 
 *  Parameter err will be set to non zero vaule if an error occurs.
 */
GString* load_freetype_font (char* file, int* err) {
	GString* bf = NULL;

	FT_Library library;
	FT_Face face;
	int error;
	FT_Glyph glyph;
	FT_UInt glyph_index;

	error = FT_Init_FreeType (&library);
	if (error != OK) {
		g_warning ("Freetype init error %d.\n", error);
		*err = error;
		return bf;
	}

	error = FT_New_Face (library, file, 0, &face);
	if (error) {
		g_warning ("Freetype font face error %d\n", error);
		*err = error;
		return bf;
	}

	error = FT_Select_Charmap (face , FT_ENCODING_UNICODE);
	if (error) {
		g_warning ("Freetype can not use Unicode, error: %d\n", error);
		*err = error;
		return bf;
	}

	error = FT_Set_Char_Size (face, 0, 800, 300, 300);
	if (error) {
		g_warning ("Freetype FT_Set_Char_Size failed, error: %d.\n", error);
		*err = error;
		return bf;
	}

	bf = get_bf_font (face, file, &error);
	if (error != OK) {
		g_warning ("Failed to parse font.\n");
		*err = error;
		return bf;	
	}

	FT_Done_Face ( face );
	FT_Done_FreeType( library );
	
	*err = OK;
	return bf;
}

