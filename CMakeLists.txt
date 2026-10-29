cmake_minimum_required(VERSION 3.25)
project(ClassicImeSwitcher VERSION 0.1.0 LANGUAGES CXX)

# The build helper probes the localized MSVC prefix using PowerShell's UTF-8
# decoding; some CMake/Chinese-console combinations otherwise misdecode it.
if(DEFINED CLASSIC_MSVC_DEPENDENCY_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CLASSIC_MSVC_DEPENDENCY_PREFIX}")
endif()

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "ClassicImeSwitcher requires Windows and MSVC.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets Network Quick QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.12)

qt_add_library(ImeCore STATIC
    src/InputProfile.h src/InputProfile.cpp
    src/WindowsInput.h src/WindowsInput.cpp
    src/SwitchEngine.h src/SwitchEngine.cpp
    src/StartupTask.h src/StartupTask.cpp
)
target_include_directories(ImeCore PUBLIC src)
target_compile_definitions(ImeCore PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ImeCore PUBLIC /utf-8 /W4 /permissive-)
target_link_libraries(ImeCore PUBLIC Qt6::Core Qt6::Gui user32 imm32 ole32 oleaut32 uuid advapi32 taskschd comsuppw)

qt_add_executable(ClassicImeSwitcher WIN32
    src/main.cpp src/AppController.h src/AppController.cpp
    src/SmokeTest.h src/SmokeTest.cpp
)
qt_add_qml_module(ClassicImeSwitcher
    URI ClassicIme
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Panel.qml
        qml/ActionButton.qml
)
target_link_libraries(ClassicImeSwitcher PRIVATE ImeCore Qt6::Widgets Qt6::Network Qt6::Quick Qt6::QuickControls2)
set_target_properties(ClassicImeSwitcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    QT_WINDOWS_APP_PROJECT_EXECUTION_LEVEL requireAdministrator
)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(ImeCoreTests tests/tst_imecore.cpp)
    set_target_properties(ImeCoreTests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(ImeCoreTests PRIVATE ImeCore Qt6::Test Qt6::Widgets)
    add_test(NAME ImeCoreTests COMMAND ImeCoreTests -o "${CMAKE_CURRENT_BINARY_DIR}/imecore-tests.txt,txt")
    set_tests_properties(ImeCoreTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME AdminManifest COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/Test-Manifest.ps1"
        -MtPath "${CMAKE_MT}" -Executable "$<TARGET_FILE:ClassicImeSwitcher>"
        -OutputPath "${CMAKE_CURRENT_BINARY_DIR}/admin.manifest")
    add_executable(NativeInputTarget WIN32 tests/NativeInputTarget.cpp)
    target_compile_definitions(NativeInputTarget PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(NativeInputTarget PRIVATE /utf-8 /W4)
    target_link_libraries(NativeInputTarget PRIVATE user32 gdi32)
    set_target_properties(NativeInputTarget PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
    install(TARGETS NativeInputTarget RUNTIME DESTINATION test-support)
endif()

install(TARGETS ClassicImeSwitcher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
qt_generate_deploy_qml_app_script(
    TARGET ClassicImeSwitcher
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    NO_COMPILER_RUNTIME
)
install(SCRIPT ${deploy_script})
