cmake_minimum_required(VERSION 3.28)

project(Scraps VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32)
  message(FATAL_ERROR "Scraps 当前只支持 Windows。")
endif()

set(SCRAPS_CORE_SOURCES
  src/common/ClipboardSuppression.cpp
  src/common/FileHash.cpp
  src/common/Log.cpp
  src/common/Paths.cpp
  src/common/StringUtil.cpp
  src/common/Utf8.cpp
  src/common/ThreadPool.cpp
  src/core/actions/ActionResolver.cpp
  src/core/browser/ClipBrowserService.cpp
  src/core/browser/ClipQueryBuilder.cpp
  src/core/browser/QuickPasteQuery.cpp
  src/core/clipboard/ClipboardCapture.cpp
  src/core/clipboard/ClipboardFormat.cpp
  src/core/clipboard/ClipboardWriter.cpp
  src/core/clipboard/ContentProfile.cpp
  src/core/clipboard/OpenTargetResolver.cpp
  src/core/ocr/ImageOcrProvider.cpp
  src/core/qr/UrlQrGenerator.cpp
  src/core/commands/AppController.Clipboard.cpp
  src/core/commands/AppController.Ingest.cpp
  src/core/commands/AppController.Library.cpp
  src/core/commands/AppController.Ocr.cpp
  src/core/commands/AppController.Paste.cpp
  src/core/commands/Command.cpp
  src/core/commands/CommandService.cpp
  src/core/paste/TextTransformEngine.cpp
  src/core/policies/ClipIdentityPolicy.cpp
  src/core/policies/ImageIngestPolicy.cpp
  src/core/policies/ClipboardRetryPolicy.cpp
  src/core/store/ClipRepository.Schema.cpp
  src/core/store/ClipRepository.Identity.cpp
  src/core/store/ClipRepository.Query.cpp
  src/core/store/ClipRepository.Mutation.cpp
  src/core/store/ClipRepository.Assets.cpp
  src/core/store/SettingsRepository.cpp
  src/ipc/NamedPipeClient.cpp
  src/ipc/NamedPipeProtocol.cpp
  src/ipc/NamedPipeServer.cpp
  src/platform/win32/ClipboardEventReceiver.cpp
  src/platform/win32/HotkeySpec.cpp
  src/platform/win32/NotifyIconHost.cpp
  src/platform/win32/ShellMessageWindow.cpp
  src/ui/D2dShell.cpp
  src/ui/ClipImagePreview.cpp
  src/ui/ClipVisualFileContent.cpp
  src/ui/D2DWindowlessTextBox.cpp
  src/ui/text/GapBuffer.cpp
  src/ui/DialogPanelWindow.cpp
  src/ui/FluentIcons.cpp
  src/ui/HoverHintWindow.cpp
  src/ui/layout/FlexNode.cpp
  src/ui/vlist/HeightIndex.cpp
  src/ui/vlist/StableSlotPool.cpp
  src/ui/vlist/RowHeightModel.cpp
  src/ui/vlist/VirtualListViewport.cpp
  src/ui/ClipVisualAssets.cpp
  src/ui/HistoryAssetLoader.cpp
  src/ui/HistoryDataController.cpp
  src/ui/PanelFocusCoordinator.cpp
  src/ui/SettingsWindow.cpp
  src/ui/settings/SettingsDrawUtil.cpp
  src/ui/settings/SettingsPanelGeneral.cpp
  src/ui/settings/SettingsPanelAppearance.cpp
  src/ui/settings/SettingsPanelHotkey.cpp
  src/ui/settings/SettingsPanelClipboard.cpp
  src/ui/settings/SettingsPanelFilter.cpp
  src/ui/settings/SettingsPanelData.cpp
  src/ui/settings/SettingsPanelAbout.cpp
  src/ui/ListScrollController.cpp
  src/ui/MainWindow.cpp
  src/ui/MainWindow.autohide.cpp
  src/ui/MainWindow.actions.cpp
  src/ui/MainWindow.bench.cpp
  src/ui/MainWindow.data.cpp
  src/ui/MainWindow.dialogs.cpp
  src/ui/MainWindow.drag.cpp
  src/ui/QuickPasteWindow.cpp
  src/ui/MainWindow.render.cpp
  src/ui/PreviewWindow.cpp
  src/ui/TextLayoutCache.cpp
  src/app/LaunchPolicy.cpp
  src/app/QuickPasteSession.cpp
  src/app/StartupTrace.cpp
  src/app/Application.cpp
  src/app/ScreenshotHostManager.cpp
  third_party/qrcodegen/qrcodegen.c
)

file(GLOB_RECURSE YOGA_SOURCES "third_party/yoga/*.cpp")

add_library(scraps_core STATIC 
  ${SCRAPS_CORE_SOURCES}
  ${YOGA_SOURCES}
)

target_include_directories(
  scraps_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qrcodegen
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yoga
)

target_compile_definitions(scraps_core
  PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  PRIVATE
    SCRAPS_ENABLE_TEST_HOOKS
)

target_link_libraries(scraps_core
  PUBLIC
    advapi32
    comctl32
    d2d1
    d3d11
    dxguid
    dwmapi
    dwrite
    gdi32
    imm32
    ole32
    Psapi
    shell32
    user32
    windowscodecs
    winsqlite3
    runtimeobject
)

add_executable(Scraps WIN32 src/app/main.cpp)
target_link_libraries(Scraps PRIVATE scraps_core)
target_link_libraries(Scraps PRIVATE dbghelp)

if(MSVC)
  target_link_options(Scraps PRIVATE
    "/DELAYLOAD:d3d11.dll"
    "/DELAYLOAD:d2d1.dll"
    "/DELAYLOAD:dcomp.dll"
    "/DELAYLOAD:dwrite.dll"
    "delayimp.lib"
  )
endif()

add_custom_command(
  TARGET Scraps POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Scraps>/assets/fluent
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fluent
    $<TARGET_FILE_DIR:Scraps>/assets/fluent
)
add_executable(scraps-cli src/cli/main.cpp src/cli/CliCommandParser.cpp)
target_link_libraries(scraps-cli PRIVATE scraps_core)

set(SCRAPS_SCREENSHOT_SOURCES
  src/screenshot/app/main.cpp
  src/screenshot/app/ScreenshotApp.cpp
  src/screenshot/app/ScreenshotSession.cpp
  src/screenshot/capture/CaptureSupervisor.cpp
  src/screenshot/capture/DxgiDuplicationBackend.cpp
  src/screenshot/capture/GdiCaptureBackend.cpp
  src/screenshot/capture/WgcCaptureBackend.cpp
  src/screenshot/editing/AnnotationDocument.cpp
  src/screenshot/editing/CommandManager.cpp
  src/screenshot/editing/InteractionStateMachine.cpp
  src/screenshot/editing/SelectionModel.cpp
  src/screenshot/editing/ToolbarUI.cpp
  src/screenshot/export/PngExporter.cpp
  src/screenshot/export/RoiComposer.cpp
  src/screenshot/overlay/DCompDeviceResources.cpp
  src/screenshot/overlay/OverlayRenderer.cpp
  src/screenshot/overlay/OverlayWindowManager.cpp
  src/screenshot/pin/PinWindow.cpp
  src/screenshot/pin/PinWindowManager.cpp
  src/screenshot/platform/CoordinateMapper.cpp
  src/screenshot/platform/MonitorTopology.cpp
  src/screenshot/platform/UiaSnappingService.cpp
  src/screenshot/platform/WindowEnumerationService.cpp
  src/screenshot/protocol/ScreenshotProtocol.cpp
)

add_executable(scraps_screenshot WIN32 ${SCRAPS_SCREENSHOT_SOURCES})
target_link_libraries(scraps_screenshot PRIVATE scraps_core)
target_include_directories(scraps_screenshot PRIVATE src/screenshot)

enable_testing()
add_executable(
  scraps_tests
  src/tests/TestMain.cpp
  src/tests/ClipIdentityPolicyTests.cpp
  src/tests/ImageIngestPolicyTests.cpp
  src/tests/ClipboardRetryPolicyTests.cpp
  src/tests/ClipRepositoryMutationTests.cpp
  src/tests/LaunchPolicyTests.cpp
  src/tests/AutoHideInteractionStateTests.cpp
  src/tests/AppControllerResolveActionsTests.cpp
  src/tests/AppControllerClipboardPolicyTests.cpp
  src/tests/PreviewWindowTests.cpp
  src/tests/RefreshRequestTests.cpp
  src/tests/TextTransformEngineTests.cpp
  src/tests/UrlQrGeneratorTests.cpp
  src/tests/OpenTargetResolverTests.cpp
  src/tests/ClipQueryBuilderTests.cpp
  src/tests/ClipImagePreviewTests.cpp
  src/tests/ClipVisualFileContentTests.cpp
  src/tests/GroupMenuCommandTests.cpp
  src/tests/HotkeySpecTests.cpp
  src/tests/NamedPipeProtocolTests.cpp
  src/tests/ClipboardWriterTests.cpp
  src/tests/VirtualListTests.cpp
)
target_link_libraries(scraps_tests PRIVATE scraps_core)
target_compile_definitions(scraps_tests PRIVATE SCRAPS_ENABLE_TEST_HOOKS)
add_test(NAME scraps_tests COMMAND scraps_tests)

if(MSVC)
  foreach(target_name IN ITEMS scraps_core Scraps scraps-cli scraps_tests scraps_screenshot)
    target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(${target_name} PRIVATE /sdl /guard:cf)
    target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:/Zi>)
  endforeach()
  foreach(target_name IN ITEMS Scraps scraps-cli scraps_tests scraps_screenshot)
    target_link_options(${target_name} PRIVATE /GUARD:CF /DYNAMICBASE /NXCOMPAT)
    target_link_options(${target_name} PRIVATE $<$<CONFIG:Release>:/DEBUG>)
  endforeach()
else()
  foreach(target_name IN ITEMS scraps_core Scraps scraps-cli scraps_tests scraps_screenshot)
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endforeach()
endif()

if(MSVC)
  set_property(TARGET Scraps PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_property(TARGET scraps-cli PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_property(TARGET scraps_tests PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_property(TARGET scraps_screenshot PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# add_executable(dump_parser dump_parser.cpp)
# target_link_libraries(dump_parser PRIVATE dbghelp)
