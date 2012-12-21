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
namespace Supplement {

public class Preferences {
		
	static HashTable<string, string> data;

	public Preferences () {
		data = new HashTable<string, string> (str_hash, str_equal);
	}

	public static void set_last_file (string fn) {
		set ("last_file", fn);
	}

	public static string @get (string k) {
		string? s;
		
		lock (data) {
			s = data.lookup (k);
		}
		
		return (s != null) ? (!) s : "";
	}

	public static void @set (string k, string v) {
		lock (data) {
			data.replace (k, v);
			save (); // Fixa: save in separate io thread instead.
		}
	}

	public static string[] get_recent_files () {
		string recent = get ("recent_files");
		string[] files = recent.split ("\t");
		
		for (uint i = 0; i < files.length; i++) {
			files[i] = files[i].replace ("\\t", "\t");
		}
		
		return files;
	}

	public static void add_recent_files (string file) {
		string escaped_string = file.replace ("\t", "\\t");
		StringBuilder recent = new StringBuilder ();

		foreach (string f in get_recent_files ()) {
			if (f != file) {
				recent.append (f.replace ("\t", "\\t"));
				recent.append ("\t");
			}
		}

		recent.append (escaped_string);

		set ("recent_files", @"$(recent.str)");
	}

	public static int get_window_width() {
		string wp = get ("window_width");
		int w = int.parse (wp);
		return (w == 0) ? 860 : w;
	}

	public static int get_window_height() {
		int h = int.parse (get ("window_height"));
		return (h == 0) ? 500 : h;
	}
	
	public static void load () {
		File app_dir = Supplement.get_settings_directory ();
		File settings = app_dir.get_child ("settings");

		data = new HashTable<string, string> (str_hash, str_equal);

		if (!settings.query_exists ()) {
			stderr.printf ("No settings file.\n");
			return;
		}

		FileStream? settings_file = FileStream.open ((!) settings.get_path (), "r");
		
		if (settings_file == null) {
			stderr.printf ("Failed to load settings from file %s.\n", (!) settings.get_path ());
			return;
		}
		
		return_if_fail (settings_file != null);
		
		unowned FileStream b = (!) settings_file;
		
		string? l;
		l = b.read_line ();
		while ((l = b.read_line ())!= null) {
			string line;
			
			line = (!) l;
			
			if (line.get_char (0) == '#') {
				continue;
			}
			
			int i = 0;
			int s = 0;
			
			i = line.index_of_char(' ', s);
			string key = line.substring (s, i - s);

			s = i + 1;
			i = line.index_of_char('"', s);
			s = i + 1;
			i = line.index_of_char('"', s);
			string val = line.substring (s, i - s);
			
			// (key, val, description);
			
			data.insert (key, val);
		}
	}
	
	public static void save () {
		try {
			File app_dir = Supplement.get_settings_directory ();
			File settings = app_dir.get_child ("settings");

			return_if_fail (app_dir.query_exists ());
		
			if (settings.query_exists ()) {
				settings.delete ();
			}

			DataOutputStream os = new DataOutputStream(settings.create(FileCreateFlags.REPLACE_DESTINATION));
			uint8[] d;
			long written = 0;
			
			StringBuilder sb = new StringBuilder ();
			
			sb.append ("# Supplement settings\n");
			sb.append ("# Version: 1.0\n");
			
			foreach (var k in data.get_keys ()) {
				sb.append (k);
				sb.append (" \"");
				sb.append (data.lookup (k));
				sb.append ("\"\n");
			}
			
			d = sb.str.data;
				
			while (written < d.length) { 
				written += os.write (d[written:d.length]);
			}
		} catch (Error e) {
			stderr.printf ("Can not save key settings. (%s)", e.message);	
		}	
	}
}

}