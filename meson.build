project('pbsplash', 'c')
cc = meson.get_compiler('c')

deps = [
        cc.find_library('m', required : false),
        dependency('libdrm', required : true),
]

inc = [
        include_directories('include'),
]

subdir('src')
