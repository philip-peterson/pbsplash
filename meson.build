project('pbsplash', 'c')
cc = meson.get_compiler('c')

deps = [
        cc.find_library('m', required : false),
        dependency('libdrm'),
        dependency('libudev'),
]

inc = [
        include_directories('include'),
]

subdir('src')
