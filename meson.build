project('Desk',
    ['cpp'],
    version: '0.1.0',
    license: 'MIT',
    meson_version: '>=0.63.0')


cpp = meson.get_compiler('cpp')
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core', 'Gui', 'Widgets'])
layerShell_dep = cpp.find_library('LayerShellQtInterface')
inc = include_directories('./src')

headers = run_command('find', './src', '-type', 'f', '-name', '*[.h,.hpp]', check : false).stdout().strip().split('\n')
sources = run_command('find', './src', '-type', 'f', '-name', '*[.c,.cpp]', check : false).stdout().strip().split('\n')

moc_files = qt6.compile_moc(headers : ['src/main.cpp'],
                            include_directories: inc,
                            dependencies: [qt6_dep, layerShell_dep])

executable('cuarzo-desk', headers, sources, moc_files,
           include_directories: inc,
           dependencies : [qt6_dep, layerShell_dep])
