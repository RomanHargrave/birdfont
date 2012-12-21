/*
    Copyright (C) 2012 Johan Mattsson

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

using Cairo;

namespace Supplement {

public class MenuAction : GLib.Object {
	public string label;
	public DropMenu.Selected action;
	public DropMenu? parent = null;
	public int index = -1;
	bool selected = false;
	
	public MenuAction (string label) {
		this.label = label;
	}
	
	public void set_selected (bool s) {
		selected = s;
	}
	
	public virtual void draw (double x, double y, Context cr) {
		if (selected) {
			cr.save ();
			cr.set_line_join (LineJoin.ROUND);
			cr.set_line_width (12);
			cr.set_source_rgba (102/255.0, 120/255.0, 149/255.0, 1);
			cr.rectangle (x - 2, y - 9, 88, 8);
			cr.fill_preserve ();
			cr.stroke ();
			cr.restore ();			
		}
		
		cr.save ();
		cr.set_source_rgba (1, 1, 1, 1);
		
		cr.set_font_size (12);
		cr.select_font_face ("Cantarell", FontSlant.NORMAL, FontWeight.NORMAL);
		
		cr.move_to (x, y);

		cr.show_text (label);
		cr.restore ();
	}
}

}