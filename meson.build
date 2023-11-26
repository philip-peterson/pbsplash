project('pbsplash', 'c')
cc = meson.get_compiler('c')

deps = [
        cc.find_library('m', required : false),
        dependency('libudev'),
]

conf_data = configuration_data()
use_drm = get_option('drm')

src = [
        'src/animate.c',
        'src/nanosvg.c',
        'src/timespec.c',
        'src/pbsplash.c',
        'src/fb.c',
        'src/drawing.c',
]

if use_drm.enabled()
        deps += dependency('libdrm')
        src += 'src/drm.c'
        conf_data.set('CONFIG_DRM_SUPPORT', true)
else
        src += 'src/drm_stub.c'
        conf_data.set('CONFIG_DRM_SUPPORT', false)
endif

configure_file(output : 'config.h',
               configuration : conf_data)

inc = [
        include_directories('include'),
        include_directories('.')
]

executable('pbsplash', src,
        include_directories: inc,
        dependencies: deps,
        install: true)
