cmake_minimum_required(VERSION 3.16)
project(rawPSense VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(rawPSense WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/backend.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/autostart.cpp
    src/autostart.h
    resources/resources.qrc
)
target_link_libraries(rawPSense PRIVATE Qt6::Widgets)
target_compile_definitions(rawPSense PRIVATE RAWPSENSE_VERSION="${PROJECT_VERSION}")

if(WIN32)
    # Exe icon + manifest requesting administrator rights (needed for AcerGamingFunction WMI)
    target_sources(rawPSense PRIVATE src/backend_windows.cpp src/rawpsense.rc)
    target_compile_definitions(rawPSense PRIVATE UNICODE _UNICODE)
    target_link_libraries(rawPSense PRIVATE wbemuuid ole32 oleaut32)
    if(MSVC)
        # The manifest comes from rawpsense.rc
        target_link_options(rawPSense PRIVATE /MANIFEST:NO)
    endif()
else()
    target_sources(rawPSense PRIVATE src/backend_linux.cpp)
    install(TARGETS rawPSense DESTINATION bin)
    install(FILES assets/rawpsense.png DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES assets/rawpsense.desktop DESTINATION share/applications)
endif()
