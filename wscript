VERSION = '0.1.2'
APPNAME = 'birdfont'

top = '.'
out = 'build'

def options(opt):
	opt.load('compiler_c')
	opt.load('vala')
	opt.add_option('--win32', action='store_true', default=False, help='Crosscompile for Windows')
	opt.add_option('--installer', action='store_true', default=False, help='Create Windows installer')
        
def configure(conf):
	conf.load('compiler_c vala')

	conf.check_cfg(package='glib-2.0', uselib_store='GLIB',atleast_version='2.16.0', mandatory=1, args='--cflags --libs')
	conf.check_cfg(package='gio-2.0',  uselib_store='GIO', atleast_version='2.16.0', mandatory=1, args='--cflags --libs')
	conf.check_cfg(package='gtk+-2.0', uselib_store='GTK', atleast_version='2.16.0', mandatory=1, args='--cflags --libs')
	conf.check_cfg(package='libxml-2.0', uselib_store='XML', mandatory=1, args='--cflags --libs')
	conf.check_cfg(package='webkit-1.0', uselib_store='WEB', mandatory=1, args='--cflags --libs')
	
	conf.env.append_unique('VALAFLAGS', ['--thread', '--pkg', 'webkit-1.0', '--enable-experimental', '--enable-experimental-non-null', '--vapidir=../../'])

	if conf.options.win32 :
		conf.recurse('win32')
		
def build(bld):
	bld.recurse('src')

	bld.env.VERSION = VERSION
	
	write_config ()
	
	start_dir = bld.path.find_dir('./')
	bld.install_files('${PREFIX}/share/birdfont/', start_dir.ant_glob('layout/*'), cwd=start_dir, relative_trick=True)
	bld.install_files('${PREFIX}/share/birdfont/', start_dir.ant_glob('icons/*'), cwd=start_dir, relative_trick=True)
	
	bld.install_files('${PREFIX}/share/applications/', ['birdfont.desktop'])
	bld.install_files('${PREFIX}/share/icons/hicolor/48x48/apps/', ['birdfont.png'])

	if bld.options.win32:
		bld.recurse('win32')
		
def write_config ():
	f = open('./src/Config.vala', 'w+')
	f.write("// Don't edit this file – it's generated by wscript\n")
	f.write("namespace Supplement {\n")
	f.write("	public static const string VERSION = \"")
	f.write(VERSION)
	f.write("\"");
	f.write(";\n")
	f.write("}");
