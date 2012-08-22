srcdir = '.'
blddir = 'build'
VERSION = '0.1.0'

def set_options(opt):
  opt.tool_options('compiler_cxx')

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool('node_addon')
  conf.check(cflags=['-Wall'])
  conf.env.CCDEFINES_TVA = ['TVA']
  conf.env.LINKFLAGS_TVA = ['-g']
  conf.env.CPPPATH_TVA = ['/opt/tervela/include/tervelaapi']
  conf.env.LIBPATH_TVA = ['/opt/tervela/lib']
  conf.env.LIB_TVA = 'tervelaapi'
  conf.env.CPPPATH_CVV8 = ['./gyp/include/cvv8']


def build(bld):
  obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  obj.uselib = 'TVA CVV8'
  obj.target = 'tervela'
  obj.source = 'src/Tervela.cpp src/Session.cpp src/Publication.cpp src/Subscription.cpp'

