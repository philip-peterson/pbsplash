project('pbsplash', 'c')
cc = meson.get_compiler('c')

deps = [
        dependency('tfblib'),
        cc.find_library('m', required : false)
]

inc = [
        include_directories('include'),
]

subdir('src')
