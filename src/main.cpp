#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800

#include <windows.h>
#include <d3d9.h>
#include <dbghelp.h>
#include <dinput.h>
#include <intrin.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>

#ifndef _M_IX86
#error BioPatch must be built as 32-bit x86.
#endif

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#pragma comment(lib, "dbghelp.lib")

namespace {

struct Config {
    bool enable_log = true;
    bool high_precision_time_get_time = true;
    bool precise_short_sleep = true;
    DWORD short_sleep_threshold_ms = 2;
    bool crash_dumps = true;
    bool protect_unhandled_exception_filter = true;
    bool log_ini_traffic = false;
    bool log_input_traffic = false;
    bool log_timing_traffic = false;
    bool log_d3d9_traffic = false;
    bool optimize_worker_state_wait = true;
    bool optimize_ui_thread_timer_period = true;
    bool optimize_thread_join_wait = true;
    bool optimize_mt_worker_wait = true;
    bool optimize_mt_worker_state4_timer_wait = true;
    bool optimize_mt_worker_state9_wait = false;
    bool optimize_ui_thread_message_wait = true;
    bool optimize_queue_worker_yield = true;
    bool optimize_pacing_sleep_precision = true;
    bool optimize_legacy_delay_precision = true;
    bool optimize_stream_retry_backoff = true;
    bool optimize_stream_retry_yield = false;
    bool dump_hot_input_block = false;
    bool dump_wide_input_block = false;
    bool optimize_get_cursor_pos_pair = true;
    bool optimize_idle_get_cursor_pos = true;
    bool optimize_screen_to_client_pair = true;
    bool optimize_client_to_screen_origin = true;
    bool optimize_get_client_rect_pair = true;
    bool optimize_clip_cursor = true;
    bool disable_qfps_mouse_ctrl_lag = true;
};

struct TimerState {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start_qpc{};
    DWORD base_time_get_time = 0;
    bool ready = false;
};

struct PreciseSleepTimerState {
    HANDLE handle = nullptr;
    bool attempted_high_res = false;

    ~PreciseSleepTimerState() {
        if (handle != nullptr) {
            CloseHandle(handle);
            handle = nullptr;
        }
    }
};

struct InputProbeState {
    LONG get_cursor_pos_calls = 0;
    LONG forwarded_get_cursor_pos_calls = 0;
    LONG cached_get_cursor_pos_calls = 0;
    LONG idle_cached_get_cursor_pos_calls = 0;
    LONG hot_input_stack_logs = 0;
    LONG screen_to_client_calls = 0;
    LONG forwarded_screen_to_client_calls = 0;
    LONG cached_screen_to_client_calls = 0;
    LONG identity_screen_to_client_calls = 0;
    LONG client_to_screen_calls = 0;
    LONG forwarded_client_to_screen_calls = 0;
    LONG identity_client_to_screen_calls = 0;
    LONG get_client_rect_calls = 0;
    LONG forwarded_get_client_rect_calls = 0;
    LONG cached_get_client_rect_calls = 0;
    LONG clip_cursor_calls = 0;
    LONG skipped_clip_cursor_calls = 0;
    LONG mouse_acquire_calls = 0;
    LONG mouse_unacquire_calls = 0;
    LONG mouse_get_device_state_calls = 0;
    LONG mouse_get_device_data_calls = 0;
    LONG mouse_poll_calls = 0;
    LONG mouse_set_property_calls = 0;
    LONG mouse_set_data_format_calls = 0;
    LONG mouse_set_cooperative_level_calls = 0;
    LONG qfps_element_copy_calls = 0;
    LONG qfps_element_copy_identical_calls = 0;
    DWORD last_summary_tick = 0;
    POINT last_cursor_pos{};
    bool last_cursor_valid = false;
    POINT cached_get_cursor_pos{};
    bool cached_get_cursor_valid = false;
    DWORD cached_get_cursor_thread_id = 0;
    std::uintptr_t cached_get_cursor_rva = 0;
    LONGLONG cached_get_cursor_qpc = 0;
    LONGLONG last_real_get_cursor_qpc = 0;
    LONG last_mouse_dx = 0;
    LONG last_mouse_dy = 0;
    LONG last_mouse_dz = 0;
    DWORD last_mouse_buttons = 0;
    bool last_mouse_sample_valid = false;
    LONGLONG last_mouse_sample_qpc = 0;
    POINT cached_screen_to_client_in{};
    POINT cached_screen_to_client_out{};
    bool cached_screen_to_client_valid = false;
    HWND cached_screen_to_client_hwnd = nullptr;
    DWORD cached_screen_to_client_thread_id = 0;
    std::uintptr_t cached_screen_to_client_rva = 0;
    LONGLONG cached_screen_to_client_qpc = 0;
    HWND identity_screen_to_client_hwnd = nullptr;
    bool identity_screen_to_client_valid = false;
    bool identity_screen_to_client_is_identity = false;
    LONGLONG identity_screen_to_client_qpc = 0;
    RECT cached_get_client_rect{};
    bool cached_get_client_rect_valid = false;
    HWND cached_get_client_rect_hwnd = nullptr;
    DWORD cached_get_client_rect_thread_id = 0;
    std::uintptr_t cached_get_client_rect_rva = 0;
    LONGLONG cached_get_client_rect_qpc = 0;
    RECT last_clip_rect{};
    bool last_clip_valid = false;
    bool last_clip_null = false;
};

struct TimingProbeState {
    LONG sleep_calls = 0;
    LONG forwarded_sleep_calls = 0;
    LONG precise_sleep_calls = 0;
    LONG precise_sleep_waitable_calls = 0;
    LONG precise_sleep_spin_only_calls = 0;
    LONG sleep_zero_calls = 0;
    LONG sleep_ex_calls = 0;
    LONG forwarded_sleep_ex_calls = 0;
    LONG precise_sleep_ex_calls = 0;
    LONG alertable_sleep_ex_calls = 0;
    LONG alertable_sleep_ex_io_wait_calls = 0;
    LONG alertable_sleep_ex_io_wait_total_us = 0;
    LONG alertable_sleep_ex_io_wait_max_us = 0;
    LONG time_begin_period_calls = 0;
    LONG time_end_period_calls = 0;
    LONG suppressed_time_begin_period_calls = 0;
    LONG suppressed_time_end_period_calls = 0;
    LONG wait_for_single_object_calls = 0;
    LONG wait_for_single_object_zero_timeout_calls = 0;
    LONG wait_for_single_object_short_timeout_calls = 0;
    LONG wait_for_single_object_infinite_timeout_calls = 0;
    LONG wait_for_single_object_object_results = 0;
    LONG wait_for_single_object_timeout_results = 0;
    LONG wait_for_single_object_abandoned_results = 0;
    LONG wait_for_single_object_failed_results = 0;
    LONG wait_for_multiple_objects_calls = 0;
    LONG wait_for_multiple_objects_zero_timeout_calls = 0;
    LONG wait_for_multiple_objects_short_timeout_calls = 0;
    LONG wait_for_multiple_objects_infinite_timeout_calls = 0;
    LONG wait_for_multiple_objects_object_results = 0;
    LONG wait_for_multiple_objects_timeout_results = 0;
    LONG wait_for_multiple_objects_abandoned_results = 0;
    LONG wait_for_multiple_objects_failed_results = 0;
    LONG worker_state_wait_calls = 0;
    LONG worker_state_wait_woke_calls = 0;
    LONG worker_state_wait_timeout_calls = 0;
    LONG worker_state_wait_zero_skip_calls = 0;
    LONG worker_state_wake_calls = 0;
    LONG worker_state_request3_calls = 0;
    LONG worker_state_request4_calls = 0;
    LONG worker_state_request5_calls = 0;
    LONG thread_join_wait_calls = 0;
    LONG mt_worker_wait_calls = 0;
    LONG mt_worker_state4_timer_wait_calls = 0;
    LONG mt_worker_state9_wait_calls = 0;
    LONG mt_worker_state_wake_calls = 0;
    LONG mt_worker_stop_wake_calls = 0;
    LONG ui_thread_message_wait_calls = 0;
    LONG queue_worker_yield_calls = 0;
    LONG pacing_sleep_precision_calls = 0;
    LONG legacy_delay_precision_calls = 0;
    LONG stream_retry_backoff_calls = 0;
    LONG stream_retry_yield_calls = 0;
    LONG thread_join_yield_calls = 0;
    LONG timer_period_balance = 0;
    DWORD last_summary_tick = 0;
    DWORD last_sleep_ms = 0;
    DWORD last_sleep_ex_ms = 0;
    UINT last_time_begin_period = 0;
    UINT last_time_end_period = 0;
    DWORD last_wait_for_single_object_timeout = INFINITE;
    DWORD last_wait_for_single_object_result = WAIT_FAILED;
    DWORD last_wait_for_multiple_objects_count = 0;
    BOOL last_wait_for_multiple_objects_wait_all = FALSE;
    DWORD last_wait_for_multiple_objects_timeout = INFINITE;
    DWORD last_wait_for_multiple_objects_result = WAIT_FAILED;
};

struct D3DProbeState {
    LONG direct3d_create9_calls = 0;
    LONG create_device_calls = 0;
    LONG create_device_failures = 0;
    LONG reset_calls = 0;
    LONG reset_failures = 0;
    LONG test_cooperative_level_calls = 0;
    LONG test_cooperative_level_failures = 0;
    LONG present_calls = 0;
    LONG present_failures = 0;
    LONG create_additional_swap_chain_calls = 0;
    LONG create_additional_swap_chain_failures = 0;
    DWORD last_summary_tick = 0;
};

struct QfpsLagPatchState {
    DWORD last_attempt_tick = 0;
    DWORD patched_mask = 0;
};

struct StreamRetryBackoffEntry {
    LONGLONG last_qpc = 0;
    DWORD streak = 0;
};

struct StreamRetryBackoffState {
    StreamRetryBackoffEntry caller_a{};
    StreamRetryBackoffEntry caller_b{};
};

using TimeGetTimeFn = DWORD(WINAPI*)();
using TimeBeginPeriodFn = UINT(WINAPI*)(UINT);
using TimeEndPeriodFn = UINT(WINAPI*)(UINT);
using SleepFn = VOID(WINAPI*)(DWORD);
using SleepExFn = DWORD(WINAPI*)(DWORD, BOOL);
using GetExitCodeThreadFn = BOOL(WINAPI*)(HANDLE, LPDWORD);
using GetThreadIdFn = DWORD(WINAPI*)(HANDLE);
using WaitForSingleObjectFn = DWORD(WINAPI*)(HANDLE, DWORD);
using WaitForMultipleObjectsFn = DWORD(WINAPI*)(DWORD, const HANDLE*, BOOL, DWORD);
using MsgWaitForMultipleObjectsExFn = DWORD(WINAPI*)(DWORD, const HANDLE*, DWORD, DWORD, DWORD);
using WaitOnAddressFn = BOOL(WINAPI*)(volatile VOID*, PVOID, SIZE_T, DWORD);
using WakeByAddressAllFn = VOID(WINAPI*)(PVOID);
using WorkerStateDispatchFn = void(__thiscall*)(void*);
using MtWorkerRequestState4Fn = bool(__thiscall*)(void*, void*);
using MtWorkerRequestState5Fn = void(__thiscall*)(void*);
using MtWorkerRequestState8Fn = void(__thiscall*)(void*);
using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);
using ScreenToClientFn = BOOL(WINAPI*)(HWND, LPPOINT);
using ClientToScreenFn = BOOL(WINAPI*)(HWND, LPPOINT);
using GetClientRectFn = BOOL(WINAPI*)(HWND, LPRECT);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using DirectInput8CreateFn =
    HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
using Direct3DCreate9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
using GetPrivateProfileStringAFn =
    DWORD(WINAPI*)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
using WritePrivateProfileStringAFn =
    BOOL(WINAPI*)(LPCSTR, LPCSTR, LPCSTR, LPCSTR);
using SetUnhandledExceptionFilterFn =
    LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER);
using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using D3D9CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3D9*,
    UINT,
    D3DDEVTYPE,
    HWND,
    DWORD,
    D3DPRESENT_PARAMETERS*,
    IDirect3DDevice9**);
using D3D9DeviceTestCooperativeLevelFn =
    HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using D3D9DeviceCreateAdditionalSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*,
    D3DPRESENT_PARAMETERS*,
    IDirect3DSwapChain9**);
using D3D9DeviceResetFn =
    HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using D3D9DevicePresentFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*,
    const RECT*,
    const RECT*,
    HWND,
    const RGNDATA*);
using QfpsElementCopyFn = void*(__thiscall*)(void*, const void*);

HMODULE g_module = nullptr;
Config g_config{};
TimerState g_timer{};
InputProbeState g_input_probe{};
TimingProbeState g_timing_probe{};
D3DProbeState g_d3d_probe{};
QfpsLagPatchState g_qfps_lag_patch{};
thread_local StreamRetryBackoffState g_stream_retry_backoff{};
CRITICAL_SECTION g_log_lock{};
bool g_log_lock_ready = false;
CRITICAL_SECTION g_ini_probe_lock{};
bool g_ini_probe_lock_ready = false;

std::wstring g_module_path;
std::wstring g_module_dir;
std::wstring g_data_dir;
std::wstring g_dump_dir;
std::wstring g_ini_path;
std::wstring g_log_path;

TimeGetTimeFn g_original_time_get_time = nullptr;
TimeBeginPeriodFn g_original_time_begin_period = nullptr;
TimeEndPeriodFn g_original_time_end_period = nullptr;
SleepFn g_original_sleep = nullptr;
SleepExFn g_original_sleep_ex = nullptr;
GetExitCodeThreadFn g_original_get_exit_code_thread = nullptr;
GetThreadIdFn g_original_get_thread_id = nullptr;
WaitForSingleObjectFn g_original_wait_for_single_object = nullptr;
WaitForMultipleObjectsFn g_original_wait_for_multiple_objects = nullptr;
MsgWaitForMultipleObjectsExFn g_original_msg_wait_for_multiple_objects_ex = nullptr;
WaitOnAddressFn g_wait_on_address = nullptr;
WakeByAddressAllFn g_wake_by_address_all = nullptr;
GetCursorPosFn g_original_get_cursor_pos = nullptr;
ScreenToClientFn g_original_screen_to_client = nullptr;
ClientToScreenFn g_original_client_to_screen = nullptr;
GetClientRectFn g_original_get_client_rect = nullptr;
ClipCursorFn g_original_clip_cursor = nullptr;
DirectInput8CreateFn g_original_direct_input8_create = nullptr;
Direct3DCreate9Fn g_original_direct3d_create9 = nullptr;
Direct3DCreate9ExFn g_original_direct3d_create9_ex = nullptr;
GetPrivateProfileStringAFn g_original_get_private_profile_string_a = nullptr;
WritePrivateProfileStringAFn g_original_write_private_profile_string_a = nullptr;
SetUnhandledExceptionFilterFn g_original_set_unhandled_exception_filter = nullptr;
GetProcAddressFn g_original_get_proc_address = nullptr;
thread_local PreciseSleepTimerState g_precise_sleep_timer;
void* g_original_worker_state_dispatch = nullptr;
void* g_original_worker_state_request3 = nullptr;
void* g_original_worker_state_request4 = nullptr;
void* g_original_worker_state_request5 = nullptr;
void* g_original_mt_worker_request_state4 = nullptr;
void* g_original_mt_worker_request_state5 = nullptr;
void* g_original_mt_worker_request_state8 = nullptr;
D3D9CreateDeviceFn g_original_d3d9_create_device = nullptr;
D3D9DeviceTestCooperativeLevelFn g_original_d3d9_test_cooperative_level = nullptr;
D3D9DeviceCreateAdditionalSwapChainFn g_original_d3d9_create_additional_swap_chain = nullptr;
D3D9DeviceResetFn g_original_d3d9_reset = nullptr;
D3D9DevicePresentFn g_original_d3d9_present = nullptr;
void* g_original_qfps_element_copy = nullptr;
bool g_thread_join_hook_installed = false;
bool g_supported_game_build = false;

LPTOP_LEVEL_EXCEPTION_FILTER g_downstream_exception_filter = nullptr;
LONG g_dump_written = 0;
std::unordered_set<std::string> g_seen_ini_reads;
std::unordered_set<std::string> g_seen_ini_writes;
std::unordered_set<std::string> g_seen_input_events;
std::unordered_set<std::string> g_seen_timing_events;
std::unordered_set<std::string> g_seen_d3d_events;

constexpr GUID kIID_DirectInput8A = {
    0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID kIID_DirectInput8W = {
    0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID kIID_DirectInputDevice8A = {
    0x54D41080, 0xDC15, 0x4833, {0xA4, 0x1B, 0x74, 0x8F, 0x73, 0xA3, 0x81, 0x79}};
constexpr GUID kIID_DirectInputDevice8W = {
    0x54D41081, 0xDC15, 0x4833, {0xA4, 0x1B, 0x74, 0x8F, 0x73, 0xA3, 0x81, 0x79}};
constexpr GUID kIID_Direct3D9 = {
    0x81BDCBCA, 0x64D4, 0x426D, {0xAE, 0x8D, 0xAD, 0x01, 0x47, 0xF4, 0x27, 0x5C}};
constexpr GUID kIID_Direct3D9Ex = {
    0x02177241, 0x69FC, 0x400C, {0x8F, 0xF1, 0x93, 0xA4, 0x4D, 0xF6, 0x86, 0x1D}};
constexpr GUID kIID_IUnknown = {
    0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kGUID_SysMouse = {
    0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGUID_SysKeyboard = {
    0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr std::uintptr_t kGetCursorPosCallerA = 0x0082744D;
constexpr std::uintptr_t kGetCursorPosCallerB = 0x008278AE;
constexpr std::uintptr_t kGetClientRectCallerA = 0x00827426;
constexpr std::uintptr_t kGetClientRectCallerB = 0x00827581;
constexpr std::uintptr_t kScreenToClientCallerA = 0x00827463;
constexpr std::uintptr_t kScreenToClientCallerB = 0x008278C4;
constexpr std::uintptr_t kClientToScreenCallerA = 0x00827656;
constexpr std::uintptr_t kHotInputBlockStartRva = 0x00827400;
constexpr std::size_t kHotInputBlockSize = 0x700;
constexpr std::uintptr_t kWideInputBlockStartRva = 0x00827000;
constexpr std::size_t kWideInputBlockSize = 0x1000;
constexpr std::uint64_t kScreenToClientIdentityRefreshUs = 250000ULL;
constexpr std::uint64_t kIdleGetCursorPosRefreshUs = 50000ULL;
constexpr std::uint64_t kGetClientRectStableRefreshUs = 250000ULL;
constexpr DWORD kQfpsElementDescriptor = 0x01264420;
constexpr std::uintptr_t kQfpsMouseCtrlLagOffset = 0x3C;
constexpr DWORD kQfpsLagPatchIntervalMs = 250;
constexpr std::uintptr_t kQfpsElementCopyRva = 0x004A4C40;
constexpr std::size_t kQfpsElementCopyHookSize = 7;
constexpr std::uintptr_t kWorkerStateRequest3Rva = 0x00876840;
constexpr std::uintptr_t kWorkerStateRequest4Rva = 0x008768A0;
constexpr std::uintptr_t kWorkerStateRequest5Rva = 0x00876900;
constexpr std::uintptr_t kWorkerStateDispatchRva = 0x00876950;
constexpr std::size_t kWorkerStateDispatchHookSize = 10;
constexpr std::size_t kWorkerStateRequestHookSize = 11;
constexpr std::uintptr_t kWorkerStateOffset = 0x267C;
constexpr std::uintptr_t kWorkerStateArg0Offset = 0x2688;
constexpr std::uintptr_t kWorkerStateArg1Offset = 0x268C;
constexpr std::uintptr_t kWorkerStateArg2Offset = 0x2690;
constexpr std::uintptr_t kWorkerStateResultOffset = 0x2694;
constexpr DWORD kWorkerStateWaitTimeoutMs = 1;
constexpr std::uintptr_t kUiThreadTimeBeginCallerRva = 0x00875171;
constexpr std::uintptr_t kUiThreadTimeEndCallerRva = 0x008751EA;
constexpr std::uintptr_t kUiThreadSleepCallerRva = 0x00875527;
constexpr DWORD kUiThreadMessageWaitTimeoutMs = 8;
constexpr std::uintptr_t kQueueWorkerYieldSleepCallerRva = 0x00818EDB;
constexpr std::uintptr_t kPacingSleepCallerRva = 0x00819DBE;
constexpr DWORD kPacingSleepPrecisionMaxMs = 16;
constexpr std::uintptr_t kLegacyDelaySleepCallerRva17 = 0x0080112B;
constexpr std::uintptr_t kLegacyDelaySleepCallerRva121 = 0x0082100B;
constexpr std::uintptr_t kLegacyDelaySleepCallerRvaDynamic = 0x00866C89;
constexpr std::uintptr_t kLegacyDelaySleepCallerRva50 = 0x0085DD78;
constexpr std::uintptr_t kAlertableSleepExIoCallerRvaA = 0x0078601A;
constexpr std::uintptr_t kAlertableSleepExIoCallerRvaB = 0x00786150;
constexpr std::uintptr_t kAlertableSleepExIoCallerRvaC = 0x007861AD;
constexpr std::uintptr_t kAlertableSleepExIoCallerRvaD = 0x007863E1;
constexpr std::uintptr_t kAlertableSleepExIoCallerRvaE = 0x00786491;
constexpr std::uintptr_t kStreamRetrySleepCallerRvaA = 0x008205DF;
constexpr std::uintptr_t kStreamRetrySleepCallerRvaB = 0x008215C8;
constexpr DWORD kStreamRetryBackoffGapMs = 8;
constexpr DWORD kStreamRetryBackoffMediumThreshold = 8;
constexpr DWORD kStreamRetryBackoffLongThreshold = 32;
constexpr DWORD kStreamRetryBackoffMediumMs = 2;
constexpr DWORD kStreamRetryBackoffLongMs = 4;
constexpr std::uintptr_t kThreadJoinSleepCallerRva = 0x007E2AB5;
constexpr std::uintptr_t kThreadJoinSleepCallerRvaGameplayA = 0x00821BB5;
constexpr std::uintptr_t kThreadJoinSleepCallerRvaGameplayB = 0x00BCE5B7;
constexpr std::uintptr_t kThreadJoinSleepCallerRvaGameplayC = 0x00BCE60C;
constexpr std::uintptr_t kThreadJoinGetExitCodeCallerRva = 0x007E2ABF;
constexpr std::uintptr_t kThreadJoinGetExitCodeCallerRvaGameplayA = 0x00821BC2;
constexpr std::uintptr_t kThreadJoinGetExitCodeCallerRvaGameplayB = 0x00BCE5C4;
constexpr std::uintptr_t kThreadJoinGetExitCodeCallerRvaGameplayC = 0x00BCE616;
constexpr std::uintptr_t kMtWorkerSleepCallerRva = 0x00A359E6;
constexpr std::uintptr_t kMtWorkerState9SleepCallerRva = 0x00A35B04;
constexpr std::uintptr_t kMtWorkerRequestState4Rva = 0x00A35EE0;
constexpr std::uintptr_t kMtWorkerRequestState5Rva = 0x00A36920;
constexpr std::uintptr_t kMtWorkerRequestState8Rva = 0x00A36CB0;
constexpr std::size_t kMtWorkerRequestState4HookSize = 7;
constexpr std::size_t kMtWorkerRequestState5HookSize = 7;
constexpr std::size_t kMtWorkerRequestState8HookSize = 8;
constexpr std::uintptr_t kMtWorkerStopFlagOffset = 0x24;
constexpr std::uintptr_t kMtWorkerStateObjectOffset = 0x6C;
constexpr std::uintptr_t kMtWorkerInnerStateOffset = 0x2C;
constexpr std::uintptr_t kMtWorkerTimerStartOffset = 0x48;
constexpr std::uintptr_t kMtWorkerTimerActiveOffset = 0x50;
constexpr std::uintptr_t kMtWorkerTimerDurationOffset = 0x54;
constexpr DWORD kMtWorkerWaitTimeoutMs = 8;
constexpr DWORD kMtWorkerState4TimerWaitMaxMs = 8;
constexpr DWORD kMtWorkerState9WaitTimeoutMs = 10;
constexpr std::size_t kD3D9CreateDeviceVtableSlot = 16;
constexpr std::size_t kD3D9DeviceTestCooperativeLevelVtableSlot = 3;
constexpr std::size_t kD3D9DeviceCreateAdditionalSwapChainVtableSlot = 13;
constexpr std::size_t kD3D9DeviceResetVtableSlot = 16;
constexpr std::size_t kD3D9DevicePresentVtableSlot = 17;
constexpr WORD kSupportedGameMachine = IMAGE_FILE_MACHINE_I386;
constexpr DWORD kSupportedGameTimeDateStamp = 0x5C9057AC;
constexpr DWORD kSupportedGameSizeOfImage = 0x01210000;
constexpr ULONGLONG kSupportedGameFileSize = 18315248ULL;

bool g_hot_input_block_dumped = false;
bool g_wide_input_block_dumped = false;
bool g_runtime_qfps_pointer_refs_dumped = false;
bool g_runtime_qfps_call_chain_dumped = false;
bool g_runtime_qfps_live_objects_logged = false;

void Log(const char* format, ...);
bool InstallInlineHook(
    void* target,
    void* replacement,
    void** original_out,
    std::size_t stolen_size);
std::string DescribeAddress(const void* address);
std::string DescribeCallerAddress(void* return_address);
std::string DescribeCodeWindow(const void* center, std::size_t before, std::size_t after);
bool RememberInputEvent(const std::string& signature);
void MaybeLogInputSummary();
bool InstallD3D9DeviceHooks(IDirect3DDevice9* device);
DWORD WINAPI HookedTimeGetTime();
void PreciseSleepSpin(DWORD milliseconds);

std::wstring GetModulePath(HMODULE module) {
    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
    return std::wstring(buffer, buffer + length);
}

std::wstring GetDirectoryName(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(0, slash);
}

void EnsureDirectory(const std::wstring& path) {
    if (!path.empty()) {
        CreateDirectoryW(path.c_str(), nullptr);
    }
}

bool WriteBufferToFile(const std::wstring& path, const void* data, DWORD size) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == size;
}

std::string SanitizeAnsiForLog(const char* text, std::size_t max_length = 120) {
    if (text == nullptr) {
        return "<null>";
    }

    std::string result;
    result.reserve(max_length);

    for (std::size_t i = 0; text[i] != '\0' && i < max_length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            result.push_back(' ');
        } else if (ch >= 32 && ch < 127) {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('?');
        }
    }

    if (std::strlen(text) > max_length) {
        result += "...";
    }

    return result;
}

const char* GetPathBaseName(const char* path) {
    if (path == nullptr) {
        return nullptr;
    }

    const char* slash = std::strrchr(path, '\\');
    const char* alt_slash = std::strrchr(path, '/');
    const char* base = slash != nullptr ? slash + 1 : path;
    if (alt_slash != nullptr && alt_slash + 1 > base) {
        base = alt_slash + 1;
    }
    return base;
}

bool IsConfigIniPath(const char* path) {
    const char* base = GetPathBaseName(path);
    return base != nullptr && _stricmp(base, "config.ini") == 0;
}

bool RememberIniEvent(
    std::unordered_set<std::string>& seen,
    const std::string& signature) {
    if (!g_ini_probe_lock_ready) {
        return true;
    }

    EnterCriticalSection(&g_ini_probe_lock);
    const bool inserted = seen.insert(signature).second;
    LeaveCriticalSection(&g_ini_probe_lock);
    return inserted;
}

bool RememberInputEvent(const std::string& signature) {
    return RememberIniEvent(g_seen_input_events, signature);
}

bool RememberTimingEvent(const std::string& signature) {
    return RememberIniEvent(g_seen_timing_events, signature);
}

bool RememberD3DEvent(const std::string& signature) {
    return RememberIniEvent(g_seen_d3d_events, signature);
}

bool IsEqualGuidValue(REFGUID left, const GUID& right) {
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

bool IsEqualGuidRef(REFGUID left, REFGUID right) {
    return InlineIsEqualGUID(left, right) != 0;
}

std::string FormatGuid(REFGUID guid) {
    char buffer[64] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        static_cast<unsigned long>(guid.Data1),
        static_cast<unsigned int>(guid.Data2),
        static_cast<unsigned int>(guid.Data3),
        static_cast<unsigned int>(guid.Data4[0]),
        static_cast<unsigned int>(guid.Data4[1]),
        static_cast<unsigned int>(guid.Data4[2]),
        static_cast<unsigned int>(guid.Data4[3]),
        static_cast<unsigned int>(guid.Data4[4]),
        static_cast<unsigned int>(guid.Data4[5]),
        static_cast<unsigned int>(guid.Data4[6]),
        static_cast<unsigned int>(guid.Data4[7]));
    return buffer;
}

const char* KnownGuidLabel(REFGUID guid) {
    if (IsEqualGuidValue(guid, kIID_DirectInput8A)) {
        return "IID_IDirectInput8A";
    }
    if (IsEqualGuidValue(guid, kIID_DirectInput8W)) {
        return "IID_IDirectInput8W";
    }
    if (IsEqualGuidValue(guid, kIID_DirectInputDevice8A)) {
        return "IID_IDirectInputDevice8A";
    }
    if (IsEqualGuidValue(guid, kIID_DirectInputDevice8W)) {
        return "IID_IDirectInputDevice8W";
    }
    if (IsEqualGuidValue(guid, kGUID_SysMouse)) {
        return "GUID_SysMouse";
    }
    if (IsEqualGuidValue(guid, kGUID_SysKeyboard)) {
        return "GUID_SysKeyboard";
    }
    return nullptr;
}

std::string DescribeGuid(REFGUID guid) {
    if (const char* label = KnownGuidLabel(guid)) {
        return std::string(label);
    }
    return FormatGuid(guid);
}

std::string FormatHex32(DWORD value) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(value));
    return buffer;
}

bool TryStartSummaryWindow(DWORD* last_summary_tick, DWORD now) {
    auto* atomic_tick = reinterpret_cast<volatile LONG*>(last_summary_tick);
    const DWORD previous = static_cast<DWORD>(InterlockedCompareExchange(atomic_tick, 0, 0));
    if (previous != 0 && now - previous < 1000) {
        return false;
    }

    return InterlockedCompareExchange(
               atomic_tick,
               static_cast<LONG>(now),
               static_cast<LONG>(previous)) == static_cast<LONG>(previous);
}

std::string DescribeWaitTimeout(DWORD timeout) {
    if (timeout == INFINITE) {
        return "INFINITE";
    }

    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(timeout));
    return buffer;
}

LONG QpcDeltaToUs(LONGLONG start_qpc, LONGLONG end_qpc) {
    if (!g_timer.ready || end_qpc <= start_qpc || g_timer.frequency.QuadPart <= 0) {
        return 0;
    }

    const auto delta_qpc = static_cast<unsigned long long>(end_qpc - start_qpc);
    const auto frequency = static_cast<unsigned long long>(g_timer.frequency.QuadPart);
    const auto delta_us = delta_qpc * 1000000ULL / frequency;
    return delta_us > 0x7fffffffULL ? 0x7fffffffL : static_cast<LONG>(delta_us);
}

void UpdateInterlockedMax(LONG* target, LONG value) {
    LONG current = InterlockedCompareExchange(target, 0, 0);
    while (value > current) {
        const LONG previous = InterlockedCompareExchange(target, value, current);
        if (previous == current) {
            break;
        }
        current = previous;
    }
}

std::string DescribeWaitResult(DWORD result) {
    if (result == WAIT_TIMEOUT) {
        return "WAIT_TIMEOUT";
    }
    if (result == WAIT_FAILED) {
        return "WAIT_FAILED";
    }
    if (result == WAIT_OBJECT_0) {
        return "WAIT_OBJECT_0";
    }
    if (result > WAIT_OBJECT_0 && result < WAIT_ABANDONED_0) {
        char buffer[32] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "WAIT_OBJECT_0+%lu",
            static_cast<unsigned long>(result - WAIT_OBJECT_0));
        return buffer;
    }
    if (result == WAIT_ABANDONED_0) {
        return "WAIT_ABANDONED_0";
    }
    if (result > WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + 64) {
        char buffer[40] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "WAIT_ABANDONED_0+%lu",
            static_cast<unsigned long>(result - WAIT_ABANDONED_0));
        return buffer;
    }
    return FormatHex32(result);
}

LONG* GetWorkerStatePointer(void* self) {
    if (self == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<LONG*>(
        reinterpret_cast<std::uint8_t*>(self) + kWorkerStateOffset);
}

void** GetWorkerStateArg0Pointer(void* self) {
    if (self == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void**>(
        reinterpret_cast<std::uint8_t*>(self) + kWorkerStateArg0Offset);
}

void** GetWorkerStateArg1Pointer(void* self) {
    if (self == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void**>(
        reinterpret_cast<std::uint8_t*>(self) + kWorkerStateArg1Offset);
}

void** GetWorkerStateArg2Pointer(void* self) {
    if (self == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void**>(
        reinterpret_cast<std::uint8_t*>(self) + kWorkerStateArg2Offset);
}

int* GetWorkerStateResultPointer(void* self) {
    if (self == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<int*>(
        reinterpret_cast<std::uint8_t*>(self) + kWorkerStateResultOffset);
}

bool WaitForWorkerStateChange(void* self) {
    LONG* state = GetWorkerStatePointer(self);
    if (state == nullptr) {
        return false;
    }

    const LONG expected = *state;
    if (expected == 0) {
        if (g_config.log_timing_traffic) {
            InterlockedIncrement(&g_timing_probe.worker_state_wait_zero_skip_calls);
        }
        return true;
    }

    if (g_wait_on_address == nullptr || g_wake_by_address_all == nullptr) {
        return false;
    }

    const BOOL result = g_wait_on_address(
        reinterpret_cast<volatile VOID*>(state),
        const_cast<LONG*>(&expected),
        sizeof(expected),
        kWorkerStateWaitTimeoutMs);

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.worker_state_wait_calls);
        if (result && *state != expected) {
            InterlockedIncrement(&g_timing_probe.worker_state_wait_woke_calls);
        } else {
            InterlockedIncrement(&g_timing_probe.worker_state_wait_timeout_calls);
        }
    }
    return true;
}

void WaitForWorkerStateIdle(void* self) {
    LONG* state = GetWorkerStatePointer(self);
    if (state == nullptr) {
        return;
    }

    while (*state != 0) {
        if (WaitForWorkerStateChange(self)) {
            continue;
        }

        if (g_original_sleep != nullptr) {
            g_original_sleep(1);
        } else {
            SwitchToThread();
        }
    }
}

LONG* GetMtWorkerInnerStatePointer(void* self) {
    if (self == nullptr) {
        return nullptr;
    }

    return reinterpret_cast<LONG*>(
        reinterpret_cast<std::uint8_t*>(self) + kMtWorkerInnerStateOffset);
}

bool IsMtWorkerIdleState(LONG state) {
    return state == 0 || state == 2 || state == 3 || state == 4 || state == 6 || state == 9;
}

bool TryResolveMtWorkerInnerFromOuter(void* outer, void** inner_out, LONG** state_out) {
    if (outer == nullptr || inner_out == nullptr || state_out == nullptr) {
        return false;
    }

    __try {
        auto* outer_bytes = reinterpret_cast<std::uint8_t*>(outer);
        const BYTE stop_flag = *(outer_bytes + kMtWorkerStopFlagOffset);
        if (stop_flag > 1) {
            return false;
        }

        void* inner = *reinterpret_cast<void**>(outer_bytes + kMtWorkerStateObjectOffset);
        if (inner == nullptr) {
            return false;
        }

        LONG* state = GetMtWorkerInnerStatePointer(inner);
        if (state == nullptr) {
            return false;
        }

        const LONG value = *state;
        if (value < 0 || value > 16) {
            return false;
        }

        *inner_out = inner;
        *state_out = state;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

LONG* TryGetMtWorkerStatePointerFromOuter(void* outer) {
    void* inner = nullptr;
    LONG* state = nullptr;
    return TryResolveMtWorkerInnerFromOuter(outer, &inner, &state) ? state : nullptr;
}

bool SafeReadLong(const LONG* address, LONG* value_out) {
    if (address == nullptr || value_out == nullptr) {
        return false;
    }

    __try {
        *value_out = *address;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadByte(const BYTE* address, BYTE* value_out) {
    if (address == nullptr || value_out == nullptr) {
        return false;
    }

    __try {
        *value_out = *address;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWaitOnAddressLong(LONG* address, LONG expected, DWORD timeout, BOOL* result_out) {
    if (address == nullptr || result_out == nullptr || g_wait_on_address == nullptr) {
        return false;
    }

    __try {
        *result_out = g_wait_on_address(
            reinterpret_cast<volatile VOID*>(address),
            &expected,
            sizeof(expected),
            timeout);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWakeByAddressLong(LONG* address) {
    if (address == nullptr || g_wake_by_address_all == nullptr) {
        return false;
    }

    __try {
        g_wake_by_address_all(reinterpret_cast<PVOID>(address));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct MtWorkerWaitTarget {
    void* inner = nullptr;
    LONG* state = nullptr;
    const char* source_register = nullptr;
};

bool TryResolveMtWorkerWaitTarget(
    std::uintptr_t ebx_value,
    std::uintptr_t esi_value,
    std::uintptr_t edi_value,
    std::uintptr_t ebp_value,
    MtWorkerWaitTarget* out_target) {
    if (out_target == nullptr) {
        return false;
    }

    struct Candidate {
        std::uintptr_t value;
        const char* name;
    };

    const Candidate candidates[] = {
        {ebx_value, "ebx"},
        {esi_value, "esi"},
        {edi_value, "edi"},
        {ebp_value, "ebp"},
    };

    for (const auto& candidate : candidates) {
        if (candidate.value < 0x10000u) {
            continue;
        }

        void* inner = nullptr;
        LONG* state = nullptr;
        if (!TryResolveMtWorkerInnerFromOuter(
                reinterpret_cast<void*>(candidate.value),
                &inner,
                &state) ||
            state == nullptr) {
            continue;
        }

        out_target->inner = inner;
        out_target->state = state;
        out_target->source_register = candidate.name;
        return true;
    }

    return false;
}

bool TryWaitForMtWorkerState(
    std::uintptr_t ebx_value,
    std::uintptr_t esi_value,
    std::uintptr_t edi_value,
    std::uintptr_t ebp_value,
    void* caller) {
    if (!g_supported_game_build ||
        !g_config.optimize_mt_worker_wait ||
        g_wait_on_address == nullptr) {
        return false;
    }

    MtWorkerWaitTarget target{};
    if (!TryResolveMtWorkerWaitTarget(
            ebx_value,
            esi_value,
            edi_value,
            ebp_value,
            &target) ||
        target.state == nullptr) {
        return false;
    }

    LONG expected = 0;
    if (!SafeReadLong(target.state, &expected)) {
        return false;
    }

    if (!IsMtWorkerIdleState(expected)) {
        return false;
    }

    BOOL result = FALSE;
    if (!SafeWaitOnAddressLong(target.state, expected, kMtWorkerWaitTimeoutMs, &result)) {
        return false;
    }

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.mt_worker_wait_calls);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "MtWorkerWait|state=" + std::to_string(expected) +
            "|src=" + target.source_register +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "MtWorkerWait state=%ld src=%s caller=%s result=%s timeout=%lu",
                static_cast<long>(expected),
                target.source_register,
                caller_description.c_str(),
                result ? "woke-or-spurious" : "timeout-or-mismatch",
                static_cast<unsigned long>(kMtWorkerWaitTimeoutMs));
        }
    }

    return true;
}

bool TrySleepForMtWorkerState4Timer(
    std::uintptr_t ebx_value,
    std::uintptr_t esi_value,
    std::uintptr_t edi_value,
    std::uintptr_t ebp_value,
    void* caller) {
    if (!g_supported_game_build ||
        !g_config.optimize_mt_worker_state4_timer_wait ||
        !g_timer.ready) {
        return false;
    }

    MtWorkerWaitTarget target{};
    if (!TryResolveMtWorkerWaitTarget(
            ebx_value,
            esi_value,
            edi_value,
            ebp_value,
            &target) ||
        target.inner == nullptr ||
        target.state == nullptr) {
        return false;
    }

    LONG state = 0;
    if (!SafeReadLong(target.state, &state) || state != 4) {
        return false;
    }

    BYTE timer_active = 0;
    if (!SafeReadByte(
            reinterpret_cast<const BYTE*>(target.inner) + kMtWorkerTimerActiveOffset,
            &timer_active) ||
        timer_active == 0) {
        return false;
    }

    LONG start_tick = 0;
    LONG duration = 0;
    if (!SafeReadLong(
            reinterpret_cast<const LONG*>(
                reinterpret_cast<const std::uint8_t*>(target.inner) + kMtWorkerTimerStartOffset),
            &start_tick) ||
        !SafeReadLong(
            reinterpret_cast<const LONG*>(
                reinterpret_cast<const std::uint8_t*>(target.inner) + kMtWorkerTimerDurationOffset),
            &duration)) {
        return false;
    }

    const DWORD duration_ms = static_cast<DWORD>(duration);
    if (duration_ms <= 1) {
        return false;
    }

    // Use the same replacement time source as the hooked game path; this helper does not call Sleep.
    const DWORD now_ms = HookedTimeGetTime();
    const DWORD elapsed_ms = now_ms - static_cast<DWORD>(start_tick);
    if (duration_ms <= elapsed_ms + 1u) {
        return false;
    }

    const DWORD remaining_ms = duration_ms - elapsed_ms;
    DWORD wait_ms = remaining_ms - 1u;
    if (wait_ms > kMtWorkerState4TimerWaitMaxMs) {
        wait_ms = kMtWorkerState4TimerWaitMaxMs;
    }
    if (wait_ms == 0) {
        return false;
    }

    PreciseSleepSpin(wait_ms);

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.mt_worker_state4_timer_wait_calls);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "MtWorkerState4TimerWait|src=" + std::string(target.source_register) +
            "|wait=" + std::to_string(wait_ms) +
            "|remaining=" + std::to_string(remaining_ms) +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "MtWorkerState4TimerWait src=%s caller=%s wait=%lu remaining=%lu inner=%s",
                target.source_register,
                caller_description.c_str(),
                static_cast<unsigned long>(wait_ms),
                static_cast<unsigned long>(remaining_ms),
                DescribeAddress(target.inner).c_str());
        }
    }

    return true;
}

bool TryWaitForMtWorkerState9(
    std::uintptr_t ebx_value,
    void* caller) {
    if (!g_supported_game_build ||
        !g_config.optimize_mt_worker_state9_wait ||
        g_wait_on_address == nullptr) {
        return false;
    }

    if (ebx_value < 0x10000u) {
        return false;
    }

    LONG* state = GetMtWorkerInnerStatePointer(reinterpret_cast<void*>(ebx_value));
    if (state == nullptr) {
        return false;
    }

    LONG expected = 0;
    if (!SafeReadLong(state, &expected)) {
        return false;
    }

    if (expected == 9 || expected < 0 || expected > 16) {
        return false;
    }

    BOOL result = FALSE;
    if (!SafeWaitOnAddressLong(state, expected, kMtWorkerState9WaitTimeoutMs, &result)) {
        return false;
    }

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.mt_worker_state9_wait_calls);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "MtWorkerState9Wait|state=" + std::to_string(expected) +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "MtWorkerState9Wait state=%ld caller=%s result=%s timeout=%lu inner=%s",
                static_cast<long>(expected),
                caller_description.c_str(),
                result ? "woke-or-spurious" : "timeout-or-mismatch",
                static_cast<unsigned long>(kMtWorkerState9WaitTimeoutMs),
                DescribeAddress(reinterpret_cast<void*>(ebx_value)).c_str());
        }
    }

    return true;
}

void MaybeLogMtWorkerHotSleepState(
    std::uintptr_t ebx_value,
    std::uintptr_t esi_value,
    std::uintptr_t edi_value,
    std::uintptr_t ebp_value,
    void* caller) {
    if (!g_config.log_timing_traffic) {
        return;
    }

    MtWorkerWaitTarget target{};
    if (!TryResolveMtWorkerWaitTarget(
            ebx_value,
            esi_value,
            edi_value,
            ebp_value,
            &target) ||
        target.inner == nullptr ||
        target.state == nullptr) {
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature = "MtWorkerHotSleep|unresolved|" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log("MtWorkerHotSleep unresolved caller=%s", caller_description.c_str());
        }
        return;
    }

    LONG state = 0;
    BYTE timer_active = 0;
    LONG start_tick = 0;
    LONG duration = 0;
    const bool has_state = SafeReadLong(target.state, &state);
    const bool has_timer_active = SafeReadByte(
        reinterpret_cast<const BYTE*>(target.inner) + kMtWorkerTimerActiveOffset,
        &timer_active);
    const bool has_start_tick = SafeReadLong(
        reinterpret_cast<const LONG*>(
            reinterpret_cast<const std::uint8_t*>(target.inner) + kMtWorkerTimerStartOffset),
        &start_tick);
    const bool has_duration = SafeReadLong(
        reinterpret_cast<const LONG*>(
            reinterpret_cast<const std::uint8_t*>(target.inner) + kMtWorkerTimerDurationOffset),
        &duration);

    DWORD elapsed_ms = 0;
    if (has_start_tick) {
        elapsed_ms = HookedTimeGetTime() - static_cast<DWORD>(start_tick);
    }

    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string signature =
        "MtWorkerHotSleep|state=" + std::to_string(has_state ? state : -1) +
        "|active=" + std::to_string(has_timer_active ? timer_active : -1) +
        "|duration=" + std::to_string(has_duration ? duration : -1) +
        "|elapsed=" + std::to_string(elapsed_ms) +
        "|src=" + target.source_register;
    if (RememberTimingEvent(signature)) {
        Log(
            "MtWorkerHotSleep caller=%s src=%s inner=%s state=%ld active=%d duration=%ld elapsed=%lu",
            caller_description.c_str(),
            target.source_register,
            DescribeAddress(target.inner).c_str(),
            static_cast<long>(has_state ? state : -1),
            has_timer_active ? static_cast<int>(timer_active) : -1,
            static_cast<long>(has_duration ? duration : -1),
            static_cast<unsigned long>(elapsed_ms));
    }
}

void WakeMtWorkerState(void* self, const char* tag) {
    if (self == nullptr || g_wake_by_address_all == nullptr) {
        return;
    }

    LONG* state = GetMtWorkerInnerStatePointer(self);
    if (state == nullptr) {
        return;
    }

    if (!SafeWakeByAddressLong(state)) {
        return;
    }

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.mt_worker_state_wake_calls);
        const std::string signature = std::string(tag) + "|" + DescribeAddress(self);
        if (RememberTimingEvent(signature)) {
            Log("%s self=%s state=%s", tag, DescribeAddress(self).c_str(), DescribeAddress(state).c_str());
        }
    }
}

void MaybeWakeMtWorkerStateFromOuterRegs(
    std::uintptr_t ebx_value,
    std::uintptr_t esi_value,
    std::uintptr_t edi_value,
    std::uintptr_t ebp_value,
    void* caller) {
    if (!g_config.optimize_mt_worker_wait || g_wake_by_address_all == nullptr) {
        return;
    }

    MtWorkerWaitTarget target{};
    if (!TryResolveMtWorkerWaitTarget(
            ebx_value,
            esi_value,
            edi_value,
            ebp_value,
            &target) ||
        target.state == nullptr) {
        return;
    }

    if (!SafeWakeByAddressLong(target.state)) {
        return;
    }

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.mt_worker_stop_wake_calls);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "MtWorkerStopWake|src=" + std::string(target.source_register) +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "MtWorkerStopWake src=%s caller=%s state=%s",
                target.source_register,
                caller_description.c_str(),
                DescribeAddress(target.state).c_str());
        }
    }
}

std::string DescribeCooperativeFlags(DWORD flags) {
    std::string labels;

    auto append_flag = [&labels](const char* name) {
        if (!labels.empty()) {
            labels += "|";
        }
        labels += name;
    };

    if ((flags & DISCL_EXCLUSIVE) != 0) {
        append_flag("EXCLUSIVE");
    }
    if ((flags & DISCL_NONEXCLUSIVE) != 0) {
        append_flag("NONEXCLUSIVE");
    }
    if ((flags & DISCL_FOREGROUND) != 0) {
        append_flag("FOREGROUND");
    }
    if ((flags & DISCL_BACKGROUND) != 0) {
        append_flag("BACKGROUND");
    }
    if ((flags & DISCL_NOWINKEY) != 0) {
        append_flag("NOWINKEY");
    }

    if (labels.empty()) {
        return FormatHex32(flags);
    }

    return FormatHex32(flags) + "(" + labels + ")";
}

std::string DescribeDataFormat(LPCDIDATAFORMAT format) {
    if (format == nullptr) {
        return "<null>";
    }

    const char* label = "custom";
    if (format->dwDataSize == sizeof(DIMOUSESTATE) && format->dwNumObjs == 7) {
        label = "DIMOUSESTATE";
    } else if (format->dwDataSize == sizeof(DIMOUSESTATE2) && format->dwNumObjs == 11) {
        label = "DIMOUSESTATE2";
    } else if (format->dwDataSize == 256 && format->dwNumObjs == 256) {
        label = "DIKEYBOARD";
    }

    char buffer[160] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s size=%lu obj=%lu flags=%s data=%lu num=%lu",
        label,
        static_cast<unsigned long>(format->dwSize),
        static_cast<unsigned long>(format->dwObjSize),
        FormatHex32(format->dwFlags).c_str(),
        static_cast<unsigned long>(format->dwDataSize),
        static_cast<unsigned long>(format->dwNumObjs));
    return buffer;
}

std::string DescribePropertyGuid(REFGUID guid) {
    if (IsEqualGuidRef(guid, DIPROP_BUFFERSIZE)) {
        return "DIPROP_BUFFERSIZE";
    }
    if (IsEqualGuidRef(guid, DIPROP_AXISMODE)) {
        return "DIPROP_AXISMODE";
    }
    if (IsEqualGuidRef(guid, DIPROP_GRANULARITY)) {
        return "DIPROP_GRANULARITY";
    }
    if (IsEqualGuidRef(guid, DIPROP_RANGE)) {
        return "DIPROP_RANGE";
    }
    if (IsEqualGuidRef(guid, DIPROP_DEADZONE)) {
        return "DIPROP_DEADZONE";
    }
    if (IsEqualGuidRef(guid, DIPROP_SATURATION)) {
        return "DIPROP_SATURATION";
    }
    return DescribeGuid(guid);
}

std::string DescribePropertyHeader(LPCDIPROPHEADER header) {
    if (header == nullptr) {
        return "<null>";
    }

    char buffer[192] = {};
    if (header->dwSize >= sizeof(DIPROPDWORD)) {
        const auto* property = reinterpret_cast<const DIPROPDWORD*>(header);
        std::snprintf(
            buffer,
            sizeof(buffer),
            "size=%lu header=%lu obj=%lu how=%lu value=%lu",
            static_cast<unsigned long>(header->dwSize),
            static_cast<unsigned long>(header->dwHeaderSize),
            static_cast<unsigned long>(header->dwObj),
            static_cast<unsigned long>(header->dwHow),
            static_cast<unsigned long>(property->dwData));
    } else {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "size=%lu header=%lu obj=%lu how=%lu",
            static_cast<unsigned long>(header->dwSize),
            static_cast<unsigned long>(header->dwHeaderSize),
            static_cast<unsigned long>(header->dwObj),
            static_cast<unsigned long>(header->dwHow));
    }
    return buffer;
}

std::string DescribeDirectInputResult(HRESULT result) {
    return FormatHex32(static_cast<DWORD>(result));
}

std::string DescribeD3DResult(HRESULT result) {
    switch (result) {
    case D3D_OK:
        return "D3D_OK";
    case D3DERR_DEVICELOST:
        return "D3DERR_DEVICELOST";
    case D3DERR_DEVICENOTRESET:
        return "D3DERR_DEVICENOTRESET";
    case D3DERR_DRIVERINTERNALERROR:
        return "D3DERR_DRIVERINTERNALERROR";
    case D3DERR_INVALIDCALL:
        return "D3DERR_INVALIDCALL";
    case D3DERR_OUTOFVIDEOMEMORY:
        return "D3DERR_OUTOFVIDEOMEMORY";
    case E_OUTOFMEMORY:
        return "E_OUTOFMEMORY";
    default:
        return FormatHex32(static_cast<DWORD>(result));
    }
}

std::string DescribePresentParameters(const D3DPRESENT_PARAMETERS* parameters) {
    if (parameters == nullptr) {
        return "<null>";
    }

    char buffer[256] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "bb=%lux%lu fmt=%lu count=%lu ms=%lu/%lu swap=%lu hwnd=%s windowed=%u depthFmt=%lu flags=%s interval=%lu",
        static_cast<unsigned long>(parameters->BackBufferWidth),
        static_cast<unsigned long>(parameters->BackBufferHeight),
        static_cast<unsigned long>(parameters->BackBufferFormat),
        static_cast<unsigned long>(parameters->BackBufferCount),
        static_cast<unsigned long>(parameters->MultiSampleType),
        static_cast<unsigned long>(parameters->MultiSampleQuality),
        static_cast<unsigned long>(parameters->SwapEffect),
        DescribeAddress(parameters->hDeviceWindow).c_str(),
        parameters->Windowed ? 1u : 0u,
        static_cast<unsigned long>(parameters->AutoDepthStencilFormat),
        FormatHex32(parameters->Flags).c_str(),
        static_cast<unsigned long>(parameters->PresentationInterval));
    return buffer;
}

std::string DescribeDisplayModeEx(const D3DDISPLAYMODEEX* mode) {
    if (mode == nullptr) {
        return "<null>";
    }

    char buffer[160] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "size=%lu %lux%lu fmt=%lu refresh=%lu scanline=%lu",
        static_cast<unsigned long>(mode->Size),
        static_cast<unsigned long>(mode->Width),
        static_cast<unsigned long>(mode->Height),
        static_cast<unsigned long>(mode->Format),
        static_cast<unsigned long>(mode->RefreshRate),
        static_cast<unsigned long>(mode->ScanLineOrdering));
    return buffer;
}

std::string DescribeAddress(const void* address) {
    if (address == nullptr) {
        return "<unknown>";
    }

    HMODULE owner = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &owner) == 0 ||
        owner == nullptr) {
        char buffer[32] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%08lX",
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(address)));
        return buffer;
    }

    const auto absolute = reinterpret_cast<std::uintptr_t>(address);
    const auto owner_base = reinterpret_cast<std::uintptr_t>(owner);
    char buffer[96] = {};
    if (owner == GetModuleHandleW(nullptr)) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%08lX(game+rva=%08lX)",
            static_cast<unsigned long>(absolute),
            static_cast<unsigned long>(absolute - owner_base));
    } else if (owner == g_module) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%08lX(BioPatch+rva=%08lX)",
            static_cast<unsigned long>(absolute),
            static_cast<unsigned long>(absolute - owner_base));
    } else {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%08lX(mod=%08lX+rva=%08lX)",
            static_cast<unsigned long>(absolute),
            static_cast<unsigned long>(owner_base),
            static_cast<unsigned long>(absolute - owner_base));
    }
    return buffer;
}

std::string DescribeCallerAddress(void* return_address) {
    return DescribeAddress(return_address);
}

std::uintptr_t GetCallerRva(void* return_address) {
    if (return_address == nullptr) {
        return 0;
    }

    const auto caller = reinterpret_cast<std::uintptr_t>(return_address);
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (module == 0 || caller < module) {
        return 0;
    }
    return caller - module;
}

bool IsSupportedGameBuild(HMODULE main_module) {
    if (main_module == nullptr) {
        return false;
    }

    WORD machine = 0;
    DWORD time_date_stamp = 0;
    DWORD size_of_image = 0;

    IMAGE_DOS_HEADER dos_header{};
    SIZE_T bytes_read = 0;
    if (ReadProcessMemory(
            GetCurrentProcess(),
            main_module,
            &dos_header,
            sizeof(dos_header),
            &bytes_read) == FALSE ||
        bytes_read != sizeof(dos_header) ||
        dos_header.e_magic != IMAGE_DOS_SIGNATURE ||
        dos_header.e_lfanew <= 0 ||
        dos_header.e_lfanew > 0x100000) {
        return false;
    }

    IMAGE_NT_HEADERS32 nt_headers{};
    bytes_read = 0;
    const auto* nt_address = reinterpret_cast<const unsigned char*>(main_module) +
        dos_header.e_lfanew;
    if (ReadProcessMemory(
            GetCurrentProcess(),
            nt_address,
            &nt_headers,
            sizeof(nt_headers),
            &bytes_read) == FALSE ||
        bytes_read != sizeof(nt_headers) ||
        nt_headers.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    machine = nt_headers.FileHeader.Machine;
    time_date_stamp = nt_headers.FileHeader.TimeDateStamp;
    size_of_image = nt_headers.OptionalHeader.SizeOfImage;

    wchar_t module_path[MAX_PATH] = {};
    ULONGLONG file_size = 0;
    if (GetModuleFileNameW(main_module, module_path, static_cast<DWORD>(_countof(module_path))) != 0) {
        WIN32_FILE_ATTRIBUTE_DATA file_data{};
        if (GetFileAttributesExW(module_path, GetFileExInfoStandard, &file_data) != FALSE) {
            ULARGE_INTEGER size{};
            size.HighPart = file_data.nFileSizeHigh;
            size.LowPart = file_data.nFileSizeLow;
            file_size = size.QuadPart;
        }
    }

    const bool matches =
        machine == kSupportedGameMachine &&
        time_date_stamp == kSupportedGameTimeDateStamp &&
        size_of_image == kSupportedGameSizeOfImage &&
        file_size == kSupportedGameFileSize;
    if (!matches) {
        Log(
            "Unsupported game executable build: machine=%04X timestamp=%s image=%s size=%llu. Hard-coded RVA optimizations will stay disabled.",
            static_cast<unsigned int>(machine),
            FormatHex32(time_date_stamp).c_str(),
            FormatHex32(size_of_image).c_str(),
            static_cast<unsigned long long>(file_size));
    }
    return matches;
}

std::string DescribeCodeWindow(const void* center, std::size_t before = 12, std::size_t after = 20) {
    if (center == nullptr) {
        return "<unknown>";
    }

    std::string result;
    const auto center_address = reinterpret_cast<std::uintptr_t>(center);
    if (center_address < before) {
        return "<unreadable>";
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(center_address - before);
    MEMORY_BASIC_INFORMATION memory_info{};
    if (VirtualQuery(bytes, &memory_info, sizeof(memory_info)) == 0) {
        return "<unreadable>";
    }

    const DWORD protected_bits = memory_info.Protect & 0xff;
    if (memory_info.State != MEM_COMMIT ||
        (memory_info.Protect & PAGE_GUARD) != 0 ||
        protected_bits == PAGE_NOACCESS) {
        return "<unreadable>";
    }

    const std::size_t byte_count = before + after;
    std::string buffer(byte_count, '\0');
    SIZE_T bytes_read = 0;
    if (ReadProcessMemory(
            GetCurrentProcess(),
            bytes,
            buffer.data(),
            buffer.size(),
            &bytes_read) == FALSE ||
        bytes_read == 0) {
        return "<unreadable>";
    }

    for (std::size_t index = 0; index < bytes_read; ++index) {
        if (index == before) {
            result += "| ";
        }

        char byte_text[8] = {};
        std::snprintf(
            byte_text,
            sizeof(byte_text),
            "%02X ",
            static_cast<unsigned char>(buffer[index]));
        result += byte_text;
    }
    if (bytes_read < byte_count) {
        result += "<truncated>";
    }

    return result;
}

bool IsOptimizedGetCursorPosCaller(std::uintptr_t caller_rva) {
    return g_supported_game_build &&
           (caller_rva == kGetCursorPosCallerA || caller_rva == kGetCursorPosCallerB);
}

bool IsOptimizedGetClientRectCaller(std::uintptr_t caller_rva) {
    return g_supported_game_build &&
           (caller_rva == kGetClientRectCallerA || caller_rva == kGetClientRectCallerB);
}

bool IsOptimizedThreadJoinCaller(std::uintptr_t caller_rva) {
    return g_supported_game_build &&
           (caller_rva == kThreadJoinGetExitCodeCallerRva ||
            caller_rva == kThreadJoinGetExitCodeCallerRvaGameplayA ||
            caller_rva == kThreadJoinGetExitCodeCallerRvaGameplayB ||
            caller_rva == kThreadJoinGetExitCodeCallerRvaGameplayC);
}

bool IsThreadJoinSleepCaller(std::uintptr_t caller_rva) {
    return g_supported_game_build &&
           (caller_rva == kThreadJoinSleepCallerRva ||
            caller_rva == kThreadJoinSleepCallerRvaGameplayA ||
            caller_rva == kThreadJoinSleepCallerRvaGameplayB ||
            caller_rva == kThreadJoinSleepCallerRvaGameplayC);
}

bool IsAlertableSleepExIoCaller(std::uintptr_t caller_rva) {
    return g_supported_game_build &&
           (caller_rva == kAlertableSleepExIoCallerRvaA ||
            caller_rva == kAlertableSleepExIoCallerRvaB ||
            caller_rva == kAlertableSleepExIoCallerRvaC ||
            caller_rva == kAlertableSleepExIoCallerRvaD ||
            caller_rva == kAlertableSleepExIoCallerRvaE);
}

bool IsStreamRetrySleepCaller(std::uintptr_t caller_rva) {
    return g_supported_game_build &&
           (caller_rva == kStreamRetrySleepCallerRvaA ||
            caller_rva == kStreamRetrySleepCallerRvaB);
}

bool IsLegacyDelayPrecisionCaller(std::uintptr_t caller_rva, DWORD milliseconds) {
    if (!g_supported_game_build) {
        return false;
    }

    switch (caller_rva) {
    case kLegacyDelaySleepCallerRva17:
        return milliseconds == 17;
    case kLegacyDelaySleepCallerRva121:
        return milliseconds == 121;
    case kLegacyDelaySleepCallerRvaDynamic:
        return milliseconds != 0 && milliseconds <= 250;
    case kLegacyDelaySleepCallerRva50:
        return milliseconds == 50;
    default:
        return false;
    }
}

StreamRetryBackoffEntry* GetStreamRetryBackoffEntry(std::uintptr_t caller_rva) {
    if (!g_supported_game_build) {
        return nullptr;
    }

    if (caller_rva == kStreamRetrySleepCallerRvaA) {
        return &g_stream_retry_backoff.caller_a;
    }
    if (caller_rva == kStreamRetrySleepCallerRvaB) {
        return &g_stream_retry_backoff.caller_b;
    }
    return nullptr;
}

bool IsOptimizedScreenToClientCaller(std::uintptr_t caller_rva) {
    return g_supported_game_build &&
           (caller_rva == kScreenToClientCallerA || caller_rva == kScreenToClientCallerB);
}

bool IsOptimizedClientToScreenCaller(std::uintptr_t caller_rva) {
    return g_supported_game_build && caller_rva == kClientToScreenCallerA;
}

bool DumpInputBlock(
    std::uintptr_t start_rva,
    std::size_t size,
    const std::wstring& base_name,
    const char* label) {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (module == 0) {
        return false;
    }

    std::string buffer(size, '\0');
    const auto* source = reinterpret_cast<const void*>(module + start_rva);
    SIZE_T bytes_read = 0;
    const bool copied =
        ReadProcessMemory(
            GetCurrentProcess(),
            source,
            buffer.data(),
            buffer.size(),
            &bytes_read) != FALSE &&
        bytes_read == buffer.size();

    if (!copied) {
        Log(
            "Failed to dump %s at rva=%08lX.",
            label,
            static_cast<unsigned long>(start_rva));
        return false;
    }

    const std::wstring bin_path = g_data_dir + L"\\" + base_name + L".bin";
    char metadata[512] = {};
    std::snprintf(
        metadata,
        sizeof(metadata),
        "module_base=0x%08lX\r\nstart_rva=0x%08lX\r\nstart_va=0x%08lX\r\nsize=0x%zX\r\n"
        "get_client_rect_a=0x%08lX\r\nget_cursor_pos_a=0x%08lX\r\nscreen_to_client_a=0x%08lX\r\n"
        "get_client_rect_b=0x%08lX\r\nclient_to_screen_a=0x%08lX\r\nclip_cursor_a=0x%08lX\r\n"
        "get_cursor_pos_b=0x%08lX\r\nscreen_to_client_b=0x%08lX\r\nmouse_poll=0x%08lX\r\n"
        "mouse_get_device_state=0x%08lX\r\n",
        static_cast<unsigned long>(module),
        static_cast<unsigned long>(start_rva),
        static_cast<unsigned long>(module + start_rva),
        size,
        static_cast<unsigned long>(kGetClientRectCallerA),
        static_cast<unsigned long>(kGetCursorPosCallerA),
        static_cast<unsigned long>(kScreenToClientCallerA),
        static_cast<unsigned long>(kGetClientRectCallerB),
        static_cast<unsigned long>(kClientToScreenCallerA),
        static_cast<unsigned long>(0x00827691),
        static_cast<unsigned long>(kGetCursorPosCallerB),
        static_cast<unsigned long>(kScreenToClientCallerB),
        static_cast<unsigned long>(0x00827937),
        static_cast<unsigned long>(0x0082798A));
    const std::wstring txt_path = g_data_dir + L"\\" + base_name + L".txt";

    const bool bin_ok =
        WriteBufferToFile(bin_path, buffer.data(), static_cast<DWORD>(buffer.size()));
    const bool txt_ok =
        WriteBufferToFile(txt_path, metadata, static_cast<DWORD>(std::strlen(metadata)));

    if (bin_ok && txt_ok) {
        Log(
            "Dumped %s to %ls and %ls",
            label,
            bin_path.c_str(),
            txt_path.c_str());
    } else {
        Log("Failed to write %s dump files.", label);
    }

    return bin_ok && txt_ok;
}

void MaybeDumpHotInputBlock() {
    if (!g_config.dump_hot_input_block ||
        !g_supported_game_build ||
        g_hot_input_block_dumped ||
        g_data_dir.empty()) {
        return;
    }

    DumpInputBlock(
        kHotInputBlockStartRva,
        kHotInputBlockSize,
        L"hot_input_block_00827400",
        "hot input block");
    g_hot_input_block_dumped = true;
}

void MaybeDumpWideInputBlock() {
    if (!g_config.dump_wide_input_block ||
        !g_supported_game_build ||
        g_wide_input_block_dumped ||
        g_data_dir.empty()) {
        return;
    }

    DumpInputBlock(
        kWideInputBlockStartRva,
        kWideInputBlockSize,
        L"wide_input_block_00827000",
        "wide input block");
    g_wide_input_block_dumped = true;
}

DWORD GetMainModuleImageSize() {
    const auto* dos_header =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
    if (dos_header == nullptr || dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }

    const auto* nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        reinterpret_cast<const unsigned char*>(dos_header) + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }

    return nt_headers->OptionalHeader.SizeOfImage;
}

void MaybeDumpRuntimeQfpsPointerRefs() {
    if (!g_config.log_input_traffic ||
        !g_supported_game_build ||
        g_runtime_qfps_pointer_refs_dumped) {
        return;
    }

    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const DWORD image_size = GetMainModuleImageSize();
    if (module == 0 || image_size < sizeof(DWORD)) {
        return;
    }

    struct TargetValue {
        DWORD value;
        const char* name;
    };
    constexpr TargetValue kTargets[] = {
        {0x01470698, "uCameraQFPSDataA"},
        {0x014706E0, "uCameraQFPSDataB"},
        {0x01264420, "uCameraQFPSDescriptor"},
    };

    const auto* bytes = reinterpret_cast<const unsigned char*>(module);
    for (const auto& target : kTargets) {
        int hits = 0;
        for (DWORD offset = 0; offset + sizeof(DWORD) <= image_size; ++offset) {
            DWORD value = 0;
            std::memcpy(&value, bytes + offset, sizeof(DWORD));
            if (value != target.value) {
                continue;
            }

            const auto address = reinterpret_cast<const void*>(module + offset);
            Log(
                "RuntimeQfpsPtrRef %s value=%s ref=%s bytes=%s",
                target.name,
                FormatHex32(target.value).c_str(),
                DescribeAddress(address).c_str(),
                DescribeCodeWindow(address).c_str());
            ++hits;
            if (hits >= 32) {
                Log(
                    "RuntimeQfpsPtrRef %s truncated after %d hits.",
                    target.name,
                    hits);
                break;
            }
        }
        Log("RuntimeQfpsPtrRefSummary %s hits=%d", target.name, hits);
    }

    g_runtime_qfps_pointer_refs_dumped = true;
}

bool DumpRuntimeCodeBlock(
    std::uintptr_t start_rva,
    std::size_t size,
    const std::wstring& base_name,
    const char* label) {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (module == 0) {
        return false;
    }

    std::string buffer(size, '\0');
    const auto* source = reinterpret_cast<const void*>(module + start_rva);
    SIZE_T bytes_read = 0;
    const bool copied =
        ReadProcessMemory(
            GetCurrentProcess(),
            source,
            buffer.data(),
            buffer.size(),
            &bytes_read) != FALSE &&
        bytes_read == buffer.size();
    if (!copied) {
        Log(
            "Failed to dump %s at rva=%08lX.",
            label,
            static_cast<unsigned long>(start_rva));
        return false;
    }

    char metadata[128] = {};
    std::snprintf(
        metadata,
        sizeof(metadata),
        "module_base=0x%08lX\r\nstart_rva=0x%08lX\r\nstart_va=0x%08lX\r\nsize=0x%zX\r\n",
        static_cast<unsigned long>(module),
        static_cast<unsigned long>(start_rva),
        static_cast<unsigned long>(module + start_rva),
        size);

    const std::wstring bin_path = g_data_dir + L"\\" + base_name + L".bin";
    const std::wstring txt_path = g_data_dir + L"\\" + base_name + L".txt";
    const bool bin_ok =
        WriteBufferToFile(bin_path, buffer.data(), static_cast<DWORD>(buffer.size()));
    const bool txt_ok =
        WriteBufferToFile(txt_path, metadata, static_cast<DWORD>(std::strlen(metadata)));

    if (bin_ok && txt_ok) {
        Log("Dumped %s to %ls and %ls", label, bin_path.c_str(), txt_path.c_str());
    } else {
        Log("Failed to write %s dump files.", label);
    }
    return bin_ok && txt_ok;
}

void MaybeDumpRuntimeQfpsCallChainBlocks() {
    if (!g_config.log_input_traffic ||
        !g_supported_game_build ||
        g_runtime_qfps_call_chain_dumped ||
        g_data_dir.empty()) {
        return;
    }

    struct DumpTarget {
        std::uintptr_t start_rva;
        std::size_t size;
        const wchar_t* base_name;
        const char* label;
    };
    constexpr DumpTarget kTargets[] = {
        {0x006A2400, 0x600, L"qfps_chain_006a2400", "QFPS chain block 0x006A2400"},
        {0x00679400, 0x600, L"qfps_chain_00679400", "QFPS chain block 0x00679400"},
        {0x00690E00, 0x600, L"qfps_chain_00690e00", "QFPS chain block 0x00690E00"},
        {0x00875500, 0x600, L"qfps_chain_00875500", "QFPS chain block 0x00875500"},
    };

    for (const auto& target : kTargets) {
        DumpRuntimeCodeBlock(target.start_rva, target.size, target.base_name, target.label);
    }

    g_runtime_qfps_call_chain_dumped = true;
}

void MaybeLogRuntimeQfpsLiveObjects() {
    if (!g_config.log_input_traffic ||
        !g_supported_game_build ||
        g_runtime_qfps_live_objects_logged) {
        return;
    }

    struct TargetValue {
        std::uintptr_t address;
        const char* name;
    };
    constexpr TargetValue kTargets[] = {
        {0x01470698, "uCameraQFPSDataA"},
        {0x014706E0, "uCameraQFPSDataB"},
        {0x0155B688, "uCameraQFPSLiveA"},
        {0x0155BCC8, "uCameraQFPSLiveB"},
    };

    for (const auto& target : kTargets) {
        unsigned char buffer[64] = {};
        SIZE_T bytes_read = 0;
        const bool copied =
            ReadProcessMemory(
                GetCurrentProcess(),
                reinterpret_cast<const void*>(target.address),
                buffer,
                sizeof(buffer),
                &bytes_read) != FALSE &&
            bytes_read == sizeof(buffer);
        if (!copied) {
            Log("RuntimeQfpsLive %s address=%s read_failed", target.name, FormatHex32(static_cast<DWORD>(target.address)).c_str());
            continue;
        }

        std::string hex_text;
        hex_text.reserve(sizeof(buffer) * 3);
        for (std::size_t index = 0; index < sizeof(buffer); ++index) {
            if (index != 0) {
                hex_text += ' ';
            }
            char byte_text[4] = {};
            std::snprintf(byte_text, sizeof(byte_text), "%02X", buffer[index]);
            hex_text += byte_text;
        }

        Log(
            "RuntimeQfpsLive %s address=%s bytes=%s",
            target.name,
            FormatHex32(static_cast<DWORD>(target.address)).c_str(),
            hex_text.c_str());
    }

    g_runtime_qfps_live_objects_logged = true;
}

bool TryReadDword(std::uintptr_t address, DWORD* value) {
    if (value == nullptr) {
        return false;
    }

    __try {
        *value = *reinterpret_cast<volatile const DWORD*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryWriteDword(std::uintptr_t address, DWORD value) {
    __try {
        *reinterpret_cast<volatile DWORD*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void MaybeApplyQfpsMouseCtrlLagPatch(std::uintptr_t caller_rva) {
    if (!g_config.disable_qfps_mouse_ctrl_lag ||
        !g_supported_game_build ||
        !IsOptimizedGetCursorPosCaller(caller_rva)) {
        return;
    }

    const DWORD now = GetTickCount();
    if (g_qfps_lag_patch.last_attempt_tick != 0 &&
        now - g_qfps_lag_patch.last_attempt_tick < kQfpsLagPatchIntervalMs) {
        return;
    }
    g_qfps_lag_patch.last_attempt_tick = now;

    struct Target {
        std::uintptr_t address;
        const char* name;
    };
    constexpr Target kTargets[] = {
        {0x01470698, "uCameraQFPSDataA"},
        {0x014706E0, "uCameraQFPSDataB"},
        {0x0155B688, "uCameraQFPSLiveA"},
        {0x0155BCC8, "uCameraQFPSLiveB"},
    };

    for (std::size_t index = 0; index < ARRAYSIZE(kTargets); ++index) {
        const DWORD bit = (1u << index);
        DWORD descriptor = 0;
        if (!TryReadDword(kTargets[index].address, &descriptor) ||
            descriptor != kQfpsElementDescriptor) {
            continue;
        }

        DWORD mouse_ctrl_lag_enabled = 0;
        const std::uintptr_t field_address =
            kTargets[index].address + kQfpsMouseCtrlLagOffset;
        if (!TryReadDword(field_address, &mouse_ctrl_lag_enabled) ||
            mouse_ctrl_lag_enabled == 0) {
            continue;
        }

        if (!TryWriteDword(field_address, 0)) {
            continue;
        }

        if ((g_qfps_lag_patch.patched_mask & bit) == 0) {
            Log(
                "Applied QFPS mouse-control lag patch: %s field=%s old=%s new=%s",
                kTargets[index].name,
                FormatHex32(static_cast<DWORD>(field_address)).c_str(),
                FormatHex32(mouse_ctrl_lag_enabled).c_str(),
                FormatHex32(0).c_str());
        }
        g_qfps_lag_patch.patched_mask |= bit;
    }
}

bool IsQfpsElementPayloadIdentical(const void* destination, const void* source) {
    if (destination == nullptr || source == nullptr) {
        return false;
    }

    auto* destination_words =
        reinterpret_cast<volatile const DWORD*>(destination);
    auto* source_words = reinterpret_cast<volatile const DWORD*>(source);

    __try {
        for (std::size_t index = 1; index <= 17; ++index) {
            if (destination_words[index] != source_words[index]) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* __fastcall HookedQfpsElementCopy(void* self, void*, const void* source) {
    void* caller = _ReturnAddress();
    const bool identical = IsQfpsElementPayloadIdentical(self, source);

    if (g_config.log_input_traffic) {
        InterlockedIncrement(&g_input_probe.qfps_element_copy_calls);
        if (identical) {
            InterlockedIncrement(&g_input_probe.qfps_element_copy_identical_calls);
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "QfpsElementCopy|caller=" + caller_description + "|src=" +
            DescribeAddress(source) + "|identical=" + (identical ? "1" : "0");
        if (RememberInputEvent(signature)) {
            Log(
                "QfpsElementCopy caller=%s bytes=%s src=%s dst=%s identical=%d",
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeAddress(source).c_str(),
                DescribeAddress(self).c_str(),
                identical ? 1 : 0);
        }
        MaybeLogInputSummary();
    }

    auto* original = reinterpret_cast<QfpsElementCopyFn>(g_original_qfps_element_copy);
    if (original != nullptr) {
        return original(self, source);
    }
    return self;
}

bool IsIdentityScreenToClientWindow(HWND window) {
    if (window == nullptr || !g_timer.ready) {
        return false;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    if (g_input_probe.identity_screen_to_client_valid &&
        g_input_probe.identity_screen_to_client_hwnd == window) {
        const auto elapsed_us =
            static_cast<std::uint64_t>(now.QuadPart - g_input_probe.identity_screen_to_client_qpc) *
            1000000ULL /
            static_cast<std::uint64_t>(g_timer.frequency.QuadPart);
        if (elapsed_us <= kScreenToClientIdentityRefreshUs) {
            return g_input_probe.identity_screen_to_client_is_identity;
        }
    }

    POINT origin{};
    const BOOL result = ClientToScreen(window, &origin);
    g_input_probe.identity_screen_to_client_hwnd = window;
    g_input_probe.identity_screen_to_client_valid = result != FALSE;
    g_input_probe.identity_screen_to_client_is_identity =
        result != FALSE && origin.x == 0 && origin.y == 0;
    g_input_probe.identity_screen_to_client_qpc = now.QuadPart;
    return g_input_probe.identity_screen_to_client_is_identity;
}

DWORD BuildMouseButtonMask(const DIMOUSESTATE2& state) {
    DWORD mask = 0;
    for (std::size_t index = 0; index < ARRAYSIZE(state.rgbButtons); ++index) {
        if ((state.rgbButtons[index] & 0x80) != 0) {
            mask |= (1u << index);
        }
    }
    return mask;
}

void RecordMouseDeviceState(const DIMOUSESTATE2& state) {
    g_input_probe.last_mouse_dx = state.lX;
    g_input_probe.last_mouse_dy = state.lY;
    g_input_probe.last_mouse_dz = state.lZ;
    g_input_probe.last_mouse_buttons = BuildMouseButtonMask(state);
    g_input_probe.last_mouse_sample_valid = g_timer.ready;

    if (g_timer.ready) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        g_input_probe.last_mouse_sample_qpc = now.QuadPart;
    }
}

bool CanUseIdleGetCursorPosCache(std::uintptr_t caller_rva) {
    if (!g_config.optimize_idle_get_cursor_pos ||
        !g_supported_game_build ||
        caller_rva != kGetCursorPosCallerA ||
        !g_timer.ready ||
        !g_input_probe.cached_get_cursor_valid ||
        g_input_probe.cached_get_cursor_thread_id != GetCurrentThreadId() ||
        !g_input_probe.last_mouse_sample_valid ||
        g_input_probe.last_real_get_cursor_qpc == 0) {
        return false;
    }

    if (g_input_probe.last_mouse_dx != 0 ||
        g_input_probe.last_mouse_dy != 0 ||
        g_input_probe.last_mouse_dz != 0) {
        return false;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    const auto cursor_age_us =
        static_cast<std::uint64_t>(now.QuadPart - g_input_probe.last_real_get_cursor_qpc) *
        1000000ULL /
        static_cast<std::uint64_t>(g_timer.frequency.QuadPart);
    if (cursor_age_us > kIdleGetCursorPosRefreshUs) {
        return false;
    }

    const auto mouse_sample_age_us =
        static_cast<std::uint64_t>(now.QuadPart - g_input_probe.last_mouse_sample_qpc) *
        1000000ULL /
        static_cast<std::uint64_t>(g_timer.frequency.QuadPart);
    return mouse_sample_age_us <= kIdleGetCursorPosRefreshUs;
}

void MaybeLogHotInputStack(const char* tag) {
    if (!g_config.log_input_traffic) {
        return;
    }

    constexpr LONG kMaxHotInputStackLogs = 12;
    if (InterlockedCompareExchange(&g_input_probe.hot_input_stack_logs, 0, 0) >=
        kMaxHotInputStackLogs) {
        return;
    }

    static bool symbols_attempted = false;
    static bool symbols_ready = false;
    if (!symbols_attempted) {
        symbols_ready = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
        symbols_attempted = true;
    }
    if (!symbols_ready) {
        return;
    }

    CONTEXT context{};
    RtlCaptureContext(&context);

    STACKFRAME64 frame{};
#if defined(_M_IX86)
    constexpr DWORD kMachineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_X64)
    constexpr DWORD kMachineType = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rsp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    return;
#endif

    std::string stack_text;
    for (int index = 0; index < 8; ++index) {
        if (!StackWalk64(
                kMachineType,
                GetCurrentProcess(),
                GetCurrentThread(),
                &frame,
                &context,
                nullptr,
                SymFunctionTableAccess64,
                SymGetModuleBase64,
                nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }

        if (!stack_text.empty()) {
            stack_text += " <- ";
        }
        stack_text += DescribeAddress(
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(frame.AddrPC.Offset)));
    }

    if (stack_text.empty()) {
        return;
    }

    const std::string signature = std::string("HotInputStack|") + tag + "|" + stack_text;
    if (!RememberInputEvent(signature)) {
        return;
    }

    if (InterlockedIncrement(&g_input_probe.hot_input_stack_logs) > kMaxHotInputStackLogs) {
        return;
    }

    Log("HotInputStack %s %s", tag, stack_text.c_str());
}

bool ShouldWrapDirectInputDevice(REFGUID guid) {
    if (IsEqualGuidValue(guid, kGUID_SysMouse)) {
        return g_config.log_input_traffic ||
               (g_supported_game_build && g_config.optimize_idle_get_cursor_pos);
    }

    return g_config.log_input_traffic && IsEqualGuidValue(guid, kGUID_SysKeyboard);
}

bool PatchVtableEntry(void* object, std::size_t slot, void* replacement, void** original_out) {
    if (object == nullptr || replacement == nullptr || original_out == nullptr) {
        return false;
    }

    auto*** object_vtable = reinterpret_cast<void***>(object);
    if (object_vtable == nullptr || *object_vtable == nullptr) {
        return false;
    }

    void** vtable = *object_vtable;
    if (vtable[slot] == replacement) {
        return true;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(&vtable[slot], sizeof(vtable[slot]), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    if (*original_out == nullptr) {
        *original_out = vtable[slot];
    }

    vtable[slot] = replacement;

    DWORD ignored = 0;
    VirtualProtect(&vtable[slot], sizeof(vtable[slot]), old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtable[slot], sizeof(vtable[slot]));
    return true;
}

void MaybeLogD3DSummary() {
    if (!g_config.log_d3d9_traffic) {
        return;
    }

    const DWORD now = GetTickCount();
    if (!TryStartSummaryWindow(&g_d3d_probe.last_summary_tick, now)) {
        return;
    }
    const LONG direct3d_create9_calls =
        InterlockedExchange(&g_d3d_probe.direct3d_create9_calls, 0);
    const LONG create_device_calls =
        InterlockedExchange(&g_d3d_probe.create_device_calls, 0);
    const LONG create_device_failures =
        InterlockedExchange(&g_d3d_probe.create_device_failures, 0);
    const LONG reset_calls = InterlockedExchange(&g_d3d_probe.reset_calls, 0);
    const LONG reset_failures = InterlockedExchange(&g_d3d_probe.reset_failures, 0);
    const LONG test_cooperative_level_calls =
        InterlockedExchange(&g_d3d_probe.test_cooperative_level_calls, 0);
    const LONG test_cooperative_level_failures =
        InterlockedExchange(&g_d3d_probe.test_cooperative_level_failures, 0);
    const LONG present_calls = InterlockedExchange(&g_d3d_probe.present_calls, 0);
    const LONG present_failures = InterlockedExchange(&g_d3d_probe.present_failures, 0);
    const LONG create_additional_swap_chain_calls =
        InterlockedExchange(&g_d3d_probe.create_additional_swap_chain_calls, 0);
    const LONG create_additional_swap_chain_failures =
        InterlockedExchange(&g_d3d_probe.create_additional_swap_chain_failures, 0);

    if (direct3d_create9_calls == 0 &&
        create_device_calls == 0 &&
        create_device_failures == 0 &&
        reset_calls == 0 &&
        reset_failures == 0 &&
        test_cooperative_level_calls == 0 &&
        test_cooperative_level_failures == 0 &&
        present_calls == 0 &&
        present_failures == 0 &&
        create_additional_swap_chain_calls == 0 &&
        create_additional_swap_chain_failures == 0) {
        return;
    }

    Log(
        "D3D9 summary: Direct3DCreate9=%ld/s CreateDevice=%ld/s fail=%ld/s Reset=%ld/s fail=%ld/s "
        "TestCooperativeLevel=%ld/s fail=%ld/s Present=%ld/s fail=%ld/s CreateAdditionalSwapChain=%ld/s fail=%ld/s",
        direct3d_create9_calls,
        create_device_calls,
        create_device_failures,
        reset_calls,
        reset_failures,
        test_cooperative_level_calls,
        test_cooperative_level_failures,
        present_calls,
        present_failures,
        create_additional_swap_chain_calls,
        create_additional_swap_chain_failures);
}

void MaybeLogTimingSummary() {
    if (!g_config.log_timing_traffic) {
        return;
    }

    const DWORD now = GetTickCount();
    if (!TryStartSummaryWindow(&g_timing_probe.last_summary_tick, now)) {
        return;
    }

    const LONG sleep_calls = InterlockedExchange(&g_timing_probe.sleep_calls, 0);
    const LONG forwarded_sleep_calls =
        InterlockedExchange(&g_timing_probe.forwarded_sleep_calls, 0);
    const LONG precise_sleep_calls =
        InterlockedExchange(&g_timing_probe.precise_sleep_calls, 0);
    const LONG precise_sleep_waitable_calls =
        InterlockedExchange(&g_timing_probe.precise_sleep_waitable_calls, 0);
    const LONG precise_sleep_spin_only_calls =
        InterlockedExchange(&g_timing_probe.precise_sleep_spin_only_calls, 0);
    const LONG sleep_zero_calls = InterlockedExchange(&g_timing_probe.sleep_zero_calls, 0);
    const LONG sleep_ex_calls = InterlockedExchange(&g_timing_probe.sleep_ex_calls, 0);
    const LONG forwarded_sleep_ex_calls =
        InterlockedExchange(&g_timing_probe.forwarded_sleep_ex_calls, 0);
    const LONG precise_sleep_ex_calls =
        InterlockedExchange(&g_timing_probe.precise_sleep_ex_calls, 0);
    const LONG alertable_sleep_ex_calls =
        InterlockedExchange(&g_timing_probe.alertable_sleep_ex_calls, 0);
    const LONG alertable_sleep_ex_io_wait_calls =
        InterlockedExchange(&g_timing_probe.alertable_sleep_ex_io_wait_calls, 0);
    const LONG alertable_sleep_ex_io_wait_total_us =
        InterlockedExchange(&g_timing_probe.alertable_sleep_ex_io_wait_total_us, 0);
    const LONG alertable_sleep_ex_io_wait_max_us =
        InterlockedExchange(&g_timing_probe.alertable_sleep_ex_io_wait_max_us, 0);
    const LONG time_begin_period_calls =
        InterlockedExchange(&g_timing_probe.time_begin_period_calls, 0);
    const LONG time_end_period_calls =
        InterlockedExchange(&g_timing_probe.time_end_period_calls, 0);
    const LONG suppressed_time_begin_period_calls =
        InterlockedExchange(&g_timing_probe.suppressed_time_begin_period_calls, 0);
    const LONG suppressed_time_end_period_calls =
        InterlockedExchange(&g_timing_probe.suppressed_time_end_period_calls, 0);
    const LONG wait_for_single_object_calls =
        InterlockedExchange(&g_timing_probe.wait_for_single_object_calls, 0);
    const LONG wait_for_single_object_zero_timeout_calls =
        InterlockedExchange(&g_timing_probe.wait_for_single_object_zero_timeout_calls, 0);
    const LONG wait_for_single_object_short_timeout_calls =
        InterlockedExchange(&g_timing_probe.wait_for_single_object_short_timeout_calls, 0);
    const LONG wait_for_single_object_infinite_timeout_calls =
        InterlockedExchange(&g_timing_probe.wait_for_single_object_infinite_timeout_calls, 0);
    const LONG wait_for_single_object_object_results =
        InterlockedExchange(&g_timing_probe.wait_for_single_object_object_results, 0);
    const LONG wait_for_single_object_timeout_results =
        InterlockedExchange(&g_timing_probe.wait_for_single_object_timeout_results, 0);
    const LONG wait_for_single_object_abandoned_results =
        InterlockedExchange(&g_timing_probe.wait_for_single_object_abandoned_results, 0);
    const LONG wait_for_single_object_failed_results =
        InterlockedExchange(&g_timing_probe.wait_for_single_object_failed_results, 0);
    const LONG wait_for_multiple_objects_calls =
        InterlockedExchange(&g_timing_probe.wait_for_multiple_objects_calls, 0);
    const LONG wait_for_multiple_objects_zero_timeout_calls =
        InterlockedExchange(&g_timing_probe.wait_for_multiple_objects_zero_timeout_calls, 0);
    const LONG wait_for_multiple_objects_short_timeout_calls =
        InterlockedExchange(&g_timing_probe.wait_for_multiple_objects_short_timeout_calls, 0);
    const LONG wait_for_multiple_objects_infinite_timeout_calls =
        InterlockedExchange(&g_timing_probe.wait_for_multiple_objects_infinite_timeout_calls, 0);
    const LONG wait_for_multiple_objects_object_results =
        InterlockedExchange(&g_timing_probe.wait_for_multiple_objects_object_results, 0);
    const LONG wait_for_multiple_objects_timeout_results =
        InterlockedExchange(&g_timing_probe.wait_for_multiple_objects_timeout_results, 0);
    const LONG wait_for_multiple_objects_abandoned_results =
        InterlockedExchange(&g_timing_probe.wait_for_multiple_objects_abandoned_results, 0);
    const LONG wait_for_multiple_objects_failed_results =
        InterlockedExchange(&g_timing_probe.wait_for_multiple_objects_failed_results, 0);
    const LONG worker_state_wait_calls =
        InterlockedExchange(&g_timing_probe.worker_state_wait_calls, 0);
    const LONG worker_state_wait_woke_calls =
        InterlockedExchange(&g_timing_probe.worker_state_wait_woke_calls, 0);
    const LONG worker_state_wait_timeout_calls =
        InterlockedExchange(&g_timing_probe.worker_state_wait_timeout_calls, 0);
    const LONG worker_state_wait_zero_skip_calls =
        InterlockedExchange(&g_timing_probe.worker_state_wait_zero_skip_calls, 0);
    const LONG worker_state_wake_calls =
        InterlockedExchange(&g_timing_probe.worker_state_wake_calls, 0);
    const LONG worker_state_request3_calls =
        InterlockedExchange(&g_timing_probe.worker_state_request3_calls, 0);
    const LONG worker_state_request4_calls =
        InterlockedExchange(&g_timing_probe.worker_state_request4_calls, 0);
    const LONG worker_state_request5_calls =
        InterlockedExchange(&g_timing_probe.worker_state_request5_calls, 0);
    const LONG mt_worker_wait_calls =
        InterlockedExchange(&g_timing_probe.mt_worker_wait_calls, 0);
    const LONG mt_worker_state4_timer_wait_calls =
        InterlockedExchange(&g_timing_probe.mt_worker_state4_timer_wait_calls, 0);
    const LONG mt_worker_state9_wait_calls =
        InterlockedExchange(&g_timing_probe.mt_worker_state9_wait_calls, 0);
    const LONG mt_worker_state_wake_calls =
        InterlockedExchange(&g_timing_probe.mt_worker_state_wake_calls, 0);
    const LONG mt_worker_stop_wake_calls =
        InterlockedExchange(&g_timing_probe.mt_worker_stop_wake_calls, 0);
    const LONG ui_thread_message_wait_calls =
        InterlockedExchange(&g_timing_probe.ui_thread_message_wait_calls, 0);
    const LONG queue_worker_yield_calls =
        InterlockedExchange(&g_timing_probe.queue_worker_yield_calls, 0);
    const LONG pacing_sleep_precision_calls =
        InterlockedExchange(&g_timing_probe.pacing_sleep_precision_calls, 0);
    const LONG legacy_delay_precision_calls =
        InterlockedExchange(&g_timing_probe.legacy_delay_precision_calls, 0);
    const LONG stream_retry_backoff_calls =
        InterlockedExchange(&g_timing_probe.stream_retry_backoff_calls, 0);
    const LONG stream_retry_yield_calls =
        InterlockedExchange(&g_timing_probe.stream_retry_yield_calls, 0);
    const LONG thread_join_yield_calls =
        InterlockedExchange(&g_timing_probe.thread_join_yield_calls, 0);
    const LONG thread_join_wait_calls =
        InterlockedExchange(&g_timing_probe.thread_join_wait_calls, 0);

    if (sleep_calls == 0 &&
        forwarded_sleep_calls == 0 &&
        precise_sleep_calls == 0 &&
        precise_sleep_waitable_calls == 0 &&
        precise_sleep_spin_only_calls == 0 &&
        sleep_zero_calls == 0 &&
        sleep_ex_calls == 0 &&
        forwarded_sleep_ex_calls == 0 &&
        precise_sleep_ex_calls == 0 &&
        alertable_sleep_ex_calls == 0 &&
        alertable_sleep_ex_io_wait_calls == 0 &&
        alertable_sleep_ex_io_wait_total_us == 0 &&
        alertable_sleep_ex_io_wait_max_us == 0 &&
        time_begin_period_calls == 0 &&
        time_end_period_calls == 0 &&
        suppressed_time_begin_period_calls == 0 &&
        suppressed_time_end_period_calls == 0 &&
        wait_for_single_object_calls == 0 &&
        wait_for_single_object_zero_timeout_calls == 0 &&
        wait_for_single_object_short_timeout_calls == 0 &&
        wait_for_single_object_infinite_timeout_calls == 0 &&
        wait_for_single_object_object_results == 0 &&
        wait_for_single_object_timeout_results == 0 &&
        wait_for_single_object_abandoned_results == 0 &&
        wait_for_single_object_failed_results == 0 &&
        wait_for_multiple_objects_calls == 0 &&
        wait_for_multiple_objects_zero_timeout_calls == 0 &&
        wait_for_multiple_objects_short_timeout_calls == 0 &&
        wait_for_multiple_objects_infinite_timeout_calls == 0 &&
        wait_for_multiple_objects_object_results == 0 &&
        wait_for_multiple_objects_timeout_results == 0 &&
        wait_for_multiple_objects_abandoned_results == 0 &&
        wait_for_multiple_objects_failed_results == 0 &&
        worker_state_wait_calls == 0 &&
        worker_state_wait_woke_calls == 0 &&
        worker_state_wait_timeout_calls == 0 &&
        worker_state_wait_zero_skip_calls == 0 &&
        worker_state_wake_calls == 0 &&
        worker_state_request3_calls == 0 &&
        worker_state_request4_calls == 0 &&
        worker_state_request5_calls == 0 &&
        mt_worker_wait_calls == 0 &&
        mt_worker_state4_timer_wait_calls == 0 &&
        mt_worker_state9_wait_calls == 0 &&
        mt_worker_state_wake_calls == 0 &&
        mt_worker_stop_wake_calls == 0 &&
        ui_thread_message_wait_calls == 0 &&
        queue_worker_yield_calls == 0 &&
        pacing_sleep_precision_calls == 0 &&
        legacy_delay_precision_calls == 0 &&
        stream_retry_backoff_calls == 0 &&
        stream_retry_yield_calls == 0 &&
        thread_join_yield_calls == 0 &&
        thread_join_wait_calls == 0) {
        return;
    }

    const LONG timer_period_balance =
        InterlockedCompareExchange(&g_timing_probe.timer_period_balance, 0, 0);
    const LONG alertable_sleep_ex_io_wait_avg_us =
        alertable_sleep_ex_io_wait_calls > 0
            ? alertable_sleep_ex_io_wait_total_us / alertable_sleep_ex_io_wait_calls
            : 0;
    Log(
        "Timing summary: Sleep=%ld/s forwarded=%ld/s precise=%ld/s waitable=%ld/s spin=%ld/s zero=%ld/s "
        "SleepEx=%ld/s forwarded=%ld/s precise=%ld/s alertable=%ld/s io=%ld/s ioAvgUs=%ld ioMaxUs=%ld "
        "timeBegin=%ld/s timeEnd=%ld/s suppressed=%ld/%ld balance=%ld last_periods=%u/%u "
        "WFSO=%ld/s zero=%ld/s short=%ld/s infinite=%ld/s object=%ld/s timeout=%ld/s abandoned=%ld/s failed=%ld/s last=(to=%s,res=%s) "
        "WFMO=%ld/s zero=%ld/s short=%ld/s infinite=%ld/s object=%ld/s timeout=%ld/s abandoned=%ld/s failed=%ld/s last=(count=%lu,waitAll=%d,to=%s,res=%s) "
        "WorkerStateWait=%ld/s woke=%ld/s timeout=%ld/s zero=%ld/s wake=%ld/s req={%ld,%ld,%ld} "
        "MtWorkerWait=%ld/s mtTimer=%ld/s mtState9=%ld/s mtWake=%ld/s mtStopWake=%ld/s uiMsgWait=%ld/s queueYield=%ld/s pacingPrecise=%ld/s legacyPrecise=%ld/s streamBackoff=%ld/s streamYield=%ld/s joinYield=%ld/s join=%ld/s",
        sleep_calls,
        forwarded_sleep_calls,
        precise_sleep_calls,
        precise_sleep_waitable_calls,
        precise_sleep_spin_only_calls,
        sleep_zero_calls,
        sleep_ex_calls,
        forwarded_sleep_ex_calls,
        precise_sleep_ex_calls,
        alertable_sleep_ex_calls,
        alertable_sleep_ex_io_wait_calls,
        alertable_sleep_ex_io_wait_avg_us,
        alertable_sleep_ex_io_wait_max_us,
        time_begin_period_calls,
        time_end_period_calls,
        suppressed_time_begin_period_calls,
        suppressed_time_end_period_calls,
        timer_period_balance,
        static_cast<unsigned int>(g_timing_probe.last_time_begin_period),
        static_cast<unsigned int>(g_timing_probe.last_time_end_period),
        wait_for_single_object_calls,
        wait_for_single_object_zero_timeout_calls,
        wait_for_single_object_short_timeout_calls,
        wait_for_single_object_infinite_timeout_calls,
        wait_for_single_object_object_results,
        wait_for_single_object_timeout_results,
        wait_for_single_object_abandoned_results,
        wait_for_single_object_failed_results,
        DescribeWaitTimeout(g_timing_probe.last_wait_for_single_object_timeout).c_str(),
        DescribeWaitResult(g_timing_probe.last_wait_for_single_object_result).c_str(),
        wait_for_multiple_objects_calls,
        wait_for_multiple_objects_zero_timeout_calls,
        wait_for_multiple_objects_short_timeout_calls,
        wait_for_multiple_objects_infinite_timeout_calls,
        wait_for_multiple_objects_object_results,
        wait_for_multiple_objects_timeout_results,
        wait_for_multiple_objects_abandoned_results,
        wait_for_multiple_objects_failed_results,
        static_cast<unsigned long>(g_timing_probe.last_wait_for_multiple_objects_count),
        g_timing_probe.last_wait_for_multiple_objects_wait_all ? 1 : 0,
        DescribeWaitTimeout(g_timing_probe.last_wait_for_multiple_objects_timeout).c_str(),
        DescribeWaitResult(g_timing_probe.last_wait_for_multiple_objects_result).c_str(),
        worker_state_wait_calls,
        worker_state_wait_woke_calls,
        worker_state_wait_timeout_calls,
        worker_state_wait_zero_skip_calls,
        worker_state_wake_calls,
        worker_state_request3_calls,
        worker_state_request4_calls,
        worker_state_request5_calls,
        mt_worker_wait_calls,
        mt_worker_state4_timer_wait_calls,
        mt_worker_state9_wait_calls,
        mt_worker_state_wake_calls,
        mt_worker_stop_wake_calls,
        ui_thread_message_wait_calls,
        queue_worker_yield_calls,
        pacing_sleep_precision_calls,
        legacy_delay_precision_calls,
        stream_retry_backoff_calls,
        stream_retry_yield_calls,
        thread_join_yield_calls,
        thread_join_wait_calls);
}

void MaybeLogInputSummary() {
    if (!g_config.log_input_traffic) {
        return;
    }

    const DWORD now = GetTickCount();
    if (!TryStartSummaryWindow(&g_input_probe.last_summary_tick, now)) {
        return;
    }
    const LONG get_cursor_pos_calls = InterlockedExchange(&g_input_probe.get_cursor_pos_calls, 0);
    const LONG forwarded_get_cursor_pos_calls =
        InterlockedExchange(&g_input_probe.forwarded_get_cursor_pos_calls, 0);
    const LONG cached_get_cursor_pos_calls =
        InterlockedExchange(&g_input_probe.cached_get_cursor_pos_calls, 0);
    const LONG idle_cached_get_cursor_pos_calls =
        InterlockedExchange(&g_input_probe.idle_cached_get_cursor_pos_calls, 0);
    const LONG screen_to_client_calls =
        InterlockedExchange(&g_input_probe.screen_to_client_calls, 0);
    const LONG forwarded_screen_to_client_calls =
        InterlockedExchange(&g_input_probe.forwarded_screen_to_client_calls, 0);
    const LONG cached_screen_to_client_calls =
        InterlockedExchange(&g_input_probe.cached_screen_to_client_calls, 0);
    const LONG identity_screen_to_client_calls =
        InterlockedExchange(&g_input_probe.identity_screen_to_client_calls, 0);
    const LONG client_to_screen_calls =
        InterlockedExchange(&g_input_probe.client_to_screen_calls, 0);
    const LONG forwarded_client_to_screen_calls =
        InterlockedExchange(&g_input_probe.forwarded_client_to_screen_calls, 0);
    const LONG identity_client_to_screen_calls =
        InterlockedExchange(&g_input_probe.identity_client_to_screen_calls, 0);
    const LONG get_client_rect_calls =
        InterlockedExchange(&g_input_probe.get_client_rect_calls, 0);
    const LONG forwarded_get_client_rect_calls =
        InterlockedExchange(&g_input_probe.forwarded_get_client_rect_calls, 0);
    const LONG cached_get_client_rect_calls =
        InterlockedExchange(&g_input_probe.cached_get_client_rect_calls, 0);
    const LONG clip_cursor_calls = InterlockedExchange(&g_input_probe.clip_cursor_calls, 0);
    const LONG skipped_clip_cursor_calls =
        InterlockedExchange(&g_input_probe.skipped_clip_cursor_calls, 0);
    const LONG mouse_acquire_calls = InterlockedExchange(&g_input_probe.mouse_acquire_calls, 0);
    const LONG mouse_unacquire_calls =
        InterlockedExchange(&g_input_probe.mouse_unacquire_calls, 0);
    const LONG mouse_get_device_state_calls =
        InterlockedExchange(&g_input_probe.mouse_get_device_state_calls, 0);
    const LONG mouse_get_device_data_calls =
        InterlockedExchange(&g_input_probe.mouse_get_device_data_calls, 0);
    const LONG mouse_poll_calls = InterlockedExchange(&g_input_probe.mouse_poll_calls, 0);
    const LONG mouse_set_property_calls =
        InterlockedExchange(&g_input_probe.mouse_set_property_calls, 0);
    const LONG mouse_set_data_format_calls =
        InterlockedExchange(&g_input_probe.mouse_set_data_format_calls, 0);
    const LONG mouse_set_cooperative_level_calls =
        InterlockedExchange(&g_input_probe.mouse_set_cooperative_level_calls, 0);
    const LONG qfps_element_copy_calls =
        InterlockedExchange(&g_input_probe.qfps_element_copy_calls, 0);
    const LONG qfps_element_copy_identical_calls =
        InterlockedExchange(&g_input_probe.qfps_element_copy_identical_calls, 0);

    if (get_cursor_pos_calls == 0 &&
        forwarded_get_cursor_pos_calls == 0 &&
        cached_get_cursor_pos_calls == 0 &&
        idle_cached_get_cursor_pos_calls == 0 &&
        screen_to_client_calls == 0 &&
        forwarded_screen_to_client_calls == 0 &&
        cached_screen_to_client_calls == 0 &&
        identity_screen_to_client_calls == 0 &&
        client_to_screen_calls == 0 &&
        forwarded_client_to_screen_calls == 0 &&
        identity_client_to_screen_calls == 0 &&
        get_client_rect_calls == 0 &&
        forwarded_get_client_rect_calls == 0 &&
        cached_get_client_rect_calls == 0 &&
        clip_cursor_calls == 0 &&
        skipped_clip_cursor_calls == 0 &&
        mouse_acquire_calls == 0 &&
        mouse_unacquire_calls == 0 &&
        mouse_get_device_state_calls == 0 &&
        mouse_get_device_data_calls == 0 &&
        mouse_poll_calls == 0 &&
        mouse_set_property_calls == 0 &&
        mouse_set_data_format_calls == 0 &&
        mouse_set_cooperative_level_calls == 0 &&
        qfps_element_copy_calls == 0 &&
        qfps_element_copy_identical_calls == 0) {
        return;
    }

    if (g_input_probe.last_cursor_valid) {
        Log(
            "Input summary: GetCursorPos=%ld/s forwarded=%ld/s cached=%ld/s idle_cached=%ld/s ScreenToClient=%ld/s forwarded=%ld/s cached=%ld/s identity=%ld/s ClientToScreen=%ld/s forwarded=%ld/s identity=%ld/s GetClientRect=%ld/s forwarded=%ld/s cached=%ld/s ClipCursor=%ld/s skipped_clip=%ld/s mouse{Acquire=%ld/s Unacquire=%ld/s State=%ld/s Data=%ld/s Poll=%ld/s SetProperty=%ld/s SetFormat=%ld/s SetCoop=%ld/s raw=(%ld,%ld,%ld)} qfps{Copy=%ld/s identical=%ld/s} last_cursor=(%ld,%ld)",
            get_cursor_pos_calls,
            forwarded_get_cursor_pos_calls,
            cached_get_cursor_pos_calls,
            idle_cached_get_cursor_pos_calls,
            screen_to_client_calls,
            forwarded_screen_to_client_calls,
            cached_screen_to_client_calls,
            identity_screen_to_client_calls,
            client_to_screen_calls,
            forwarded_client_to_screen_calls,
            identity_client_to_screen_calls,
            get_client_rect_calls,
            forwarded_get_client_rect_calls,
            cached_get_client_rect_calls,
            clip_cursor_calls,
            skipped_clip_cursor_calls,
            mouse_acquire_calls,
            mouse_unacquire_calls,
            mouse_get_device_state_calls,
            mouse_get_device_data_calls,
            mouse_poll_calls,
            mouse_set_property_calls,
            mouse_set_data_format_calls,
            mouse_set_cooperative_level_calls,
            static_cast<long>(g_input_probe.last_mouse_dx),
            static_cast<long>(g_input_probe.last_mouse_dy),
            static_cast<long>(g_input_probe.last_mouse_dz),
            qfps_element_copy_calls,
            qfps_element_copy_identical_calls,
            static_cast<long>(g_input_probe.last_cursor_pos.x),
            static_cast<long>(g_input_probe.last_cursor_pos.y));
    } else {
        Log(
            "Input summary: GetCursorPos=%ld/s forwarded=%ld/s cached=%ld/s idle_cached=%ld/s ScreenToClient=%ld/s forwarded=%ld/s cached=%ld/s identity=%ld/s ClientToScreen=%ld/s forwarded=%ld/s identity=%ld/s GetClientRect=%ld/s forwarded=%ld/s cached=%ld/s ClipCursor=%ld/s skipped_clip=%ld/s mouse{Acquire=%ld/s Unacquire=%ld/s State=%ld/s Data=%ld/s Poll=%ld/s SetProperty=%ld/s SetFormat=%ld/s SetCoop=%ld/s raw=(%ld,%ld,%ld)} qfps{Copy=%ld/s identical=%ld/s}",
            get_cursor_pos_calls,
            forwarded_get_cursor_pos_calls,
            cached_get_cursor_pos_calls,
            idle_cached_get_cursor_pos_calls,
            screen_to_client_calls,
            forwarded_screen_to_client_calls,
            cached_screen_to_client_calls,
            identity_screen_to_client_calls,
            client_to_screen_calls,
            forwarded_client_to_screen_calls,
            identity_client_to_screen_calls,
            get_client_rect_calls,
            forwarded_get_client_rect_calls,
            cached_get_client_rect_calls,
            clip_cursor_calls,
            skipped_clip_cursor_calls,
            mouse_acquire_calls,
            mouse_unacquire_calls,
            mouse_get_device_state_calls,
            mouse_get_device_data_calls,
            mouse_poll_calls,
            mouse_set_property_calls,
            mouse_set_data_format_calls,
            mouse_set_cooperative_level_calls,
            static_cast<long>(g_input_probe.last_mouse_dx),
            static_cast<long>(g_input_probe.last_mouse_dy),
            static_cast<long>(g_input_probe.last_mouse_dz),
            qfps_element_copy_calls,
            qfps_element_copy_identical_calls);
    }
}

class DirectInputDevice8AProxy final : public IDirectInputDevice8A {
public:
    DirectInputDevice8AProxy(
        IDirectInputDevice8A* inner,
        std::string device_name,
        bool track_mouse)
        : inner_(inner), device_name_(std::move(device_name)), track_mouse_(track_mouse) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualGuidValue(riid, kIID_IUnknown) ||
            IsEqualGuidValue(riid, kIID_DirectInputDevice8A)) {
            *object = static_cast<IDirectInputDevice8A*>(this);
            AddRef();
            return S_OK;
        }
        return inner_->QueryInterface(riid, object);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return inner_->AddRef();
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG ref_count = inner_->Release();
        if (ref_count == 0) {
            delete this;
        }
        return ref_count;
    }

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override {
        return inner_->GetCapabilities(caps);
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(
        LPDIENUMDEVICEOBJECTSCALLBACKA callback,
        LPVOID context,
        DWORD flags) override {
        return inner_->EnumObjects(callback, context, flags);
    }

    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID guid, LPDIPROPHEADER header) override {
        return inner_->GetProperty(guid, header);
    }

    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID guid, LPCDIPROPHEADER header) override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->SetProperty(guid, header);
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_set_property_calls);
            MaybeLogInputSummary();
        }

        const std::string property_name = DescribePropertyGuid(guid);
        const std::string property_value = DescribePropertyHeader(header);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8A." + device_name_ + ".SetProperty|" + property_name + "|" + property_value +
            "|caller=" + caller_description + "|" + DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::SetProperty(%s, %s) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                property_name.c_str(),
                property_value.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE Acquire() override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->Acquire();
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_acquire_calls);
            MaybeLogInputSummary();
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8A." + device_name_ + ".Acquire|caller=" + caller_description + "|" +
            DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::Acquire caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE Unacquire() override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->Unacquire();
        if (track_mouse_) {
            g_input_probe.last_mouse_sample_valid = false;
        }
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_unacquire_calls);
            MaybeLogInputSummary();
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8A." + device_name_ + ".Unacquire|caller=" + caller_description + "|" +
            DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::Unacquire caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override {
        void* caller = _ReturnAddress();
        const std::uintptr_t caller_rva = GetCallerRva(caller);
        const HRESULT result = inner_->GetDeviceState(size, data);
        if (track_mouse_) {
            if (SUCCEEDED(result) && data != nullptr && size >= sizeof(DIMOUSESTATE2)) {
                RecordMouseDeviceState(*reinterpret_cast<const DIMOUSESTATE2*>(data));
            } else if (FAILED(result)) {
                g_input_probe.last_mouse_sample_valid = false;
            }
            if (g_supported_game_build && caller_rva == 0x0082798A) {
                MaybeLogHotInputStack("DI8A.Mouse.GetDeviceState");
            }
        }
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_get_device_state_calls);
            MaybeLogInputSummary();
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8A." + device_name_ + ".GetDeviceState|" + std::to_string(size) + "|caller=" +
            caller_description + "|" + DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::GetDeviceState(size=%lu) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                static_cast<unsigned long>(size),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceData(
        DWORD object_data_size,
        LPDIDEVICEOBJECTDATA object_data,
        LPDWORD in_out_count,
        DWORD flags) override {
        void* caller = _ReturnAddress();
        const DWORD requested_count = in_out_count != nullptr ? *in_out_count : 0;
        const HRESULT result =
            inner_->GetDeviceData(object_data_size, object_data, in_out_count, flags);
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_get_device_data_calls);
            MaybeLogInputSummary();
        }

        const DWORD actual_count = in_out_count != nullptr ? *in_out_count : 0;
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8A." + device_name_ + ".GetDeviceData|" + std::to_string(object_data_size) + "|" +
            std::to_string(requested_count) + "|" + std::to_string(actual_count) + "|" +
            FormatHex32(flags) + "|caller=" + caller_description + "|" +
            DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::GetDeviceData(obj=%lu requested=%lu actual=%lu flags=%s) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                static_cast<unsigned long>(object_data_size),
                static_cast<unsigned long>(requested_count),
                static_cast<unsigned long>(actual_count),
                FormatHex32(flags).c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT format) override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->SetDataFormat(format);
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_set_data_format_calls);
            MaybeLogInputSummary();
        }

        const std::string format_description = DescribeDataFormat(format);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8A." + device_name_ + ".SetDataFormat|" + format_description + "|caller=" +
            caller_description + "|" + DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::SetDataFormat(%s) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                format_description.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE event) override {
        return inner_->SetEventNotification(event);
    }

    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD flags) override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->SetCooperativeLevel(window, flags);
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_set_cooperative_level_calls);
            MaybeLogInputSummary();
        }

        const std::string cooperative_flags = DescribeCooperativeFlags(flags);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8A." + device_name_ + ".SetCooperativeLevel|" + cooperative_flags + "|caller=" +
            caller_description + "|" + DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::SetCooperativeLevel(%s) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                cooperative_flags.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE GetObjectInfo(
        LPDIDEVICEOBJECTINSTANCEA object,
        DWORD object_id,
        DWORD flags) override {
        return inner_->GetObjectInfo(object, object_id, flags);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA instance) override {
        return inner_->GetDeviceInfo(instance);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND window, DWORD flags) override {
        return inner_->RunControlPanel(window, flags);
    }

    HRESULT STDMETHODCALLTYPE Initialize(
        HINSTANCE instance,
        DWORD version,
        REFGUID guid) override {
        return inner_->Initialize(instance, version, guid);
    }

    HRESULT STDMETHODCALLTYPE CreateEffect(
        REFGUID effect_guid,
        LPCDIEFFECT effect,
        LPDIRECTINPUTEFFECT* out_effect,
        LPUNKNOWN outer) override {
        return inner_->CreateEffect(effect_guid, effect, out_effect, outer);
    }

    HRESULT STDMETHODCALLTYPE EnumEffects(
        LPDIENUMEFFECTSCALLBACKA callback,
        LPVOID context,
        DWORD effect_type) override {
        return inner_->EnumEffects(callback, context, effect_type);
    }

    HRESULT STDMETHODCALLTYPE GetEffectInfo(
        LPDIEFFECTINFOA effect_info,
        REFGUID effect_guid) override {
        return inner_->GetEffectInfo(effect_info, effect_guid);
    }

    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD state) override {
        return inner_->GetForceFeedbackState(state);
    }

    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD flags) override {
        return inner_->SendForceFeedbackCommand(flags);
    }

    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(
        LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback,
        LPVOID context,
        DWORD flags) override {
        return inner_->EnumCreatedEffectObjects(callback, context, flags);
    }

    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE escape) override {
        return inner_->Escape(escape);
    }

    HRESULT STDMETHODCALLTYPE Poll() override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->Poll();
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_poll_calls);
            MaybeLogInputSummary();
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8A." + device_name_ + ".Poll|caller=" + caller_description + "|" +
            DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::Poll caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE SendDeviceData(
        DWORD object_data_size,
        LPCDIDEVICEOBJECTDATA object_data,
        LPDWORD in_out_count,
        DWORD flags) override {
        return inner_->SendDeviceData(object_data_size, object_data, in_out_count, flags);
    }

    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(
        LPCSTR file_name,
        LPDIENUMEFFECTSINFILECALLBACK callback,
        LPVOID context,
        DWORD flags) override {
        return inner_->EnumEffectsInFile(file_name, callback, context, flags);
    }

    HRESULT STDMETHODCALLTYPE WriteEffectToFile(
        LPCSTR file_name,
        DWORD entries,
        LPDIFILEEFFECT effects,
        DWORD flags) override {
        return inner_->WriteEffectToFile(file_name, entries, effects, flags);
    }

    HRESULT STDMETHODCALLTYPE BuildActionMap(
        LPDIACTIONFORMATA action_format,
        LPCSTR user_name,
        DWORD flags) override {
        return inner_->BuildActionMap(action_format, user_name, flags);
    }

    HRESULT STDMETHODCALLTYPE SetActionMap(
        LPDIACTIONFORMATA action_format,
        LPCSTR user_name,
        DWORD flags) override {
        return inner_->SetActionMap(action_format, user_name, flags);
    }

    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA image_info) override {
        return inner_->GetImageInfo(image_info);
    }

private:
    IDirectInputDevice8A* inner_ = nullptr;
    std::string device_name_;
    bool track_mouse_ = false;
};

class DirectInputDevice8WProxy final : public IDirectInputDevice8W {
public:
    DirectInputDevice8WProxy(
        IDirectInputDevice8W* inner,
        std::string device_name,
        bool track_mouse)
        : inner_(inner), device_name_(std::move(device_name)), track_mouse_(track_mouse) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualGuidValue(riid, kIID_IUnknown) ||
            IsEqualGuidValue(riid, kIID_DirectInputDevice8W)) {
            *object = static_cast<IDirectInputDevice8W*>(this);
            AddRef();
            return S_OK;
        }
        return inner_->QueryInterface(riid, object);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return inner_->AddRef();
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG ref_count = inner_->Release();
        if (ref_count == 0) {
            delete this;
        }
        return ref_count;
    }

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override {
        return inner_->GetCapabilities(caps);
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(
        LPDIENUMDEVICEOBJECTSCALLBACKW callback,
        LPVOID context,
        DWORD flags) override {
        return inner_->EnumObjects(callback, context, flags);
    }

    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID guid, LPDIPROPHEADER header) override {
        return inner_->GetProperty(guid, header);
    }

    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID guid, LPCDIPROPHEADER header) override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->SetProperty(guid, header);
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_set_property_calls);
            MaybeLogInputSummary();
        }

        const std::string property_name = DescribePropertyGuid(guid);
        const std::string property_value = DescribePropertyHeader(header);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8W." + device_name_ + ".SetProperty|" + property_name + "|" + property_value +
            "|caller=" + caller_description + "|" + DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::SetProperty(%s, %s) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                property_name.c_str(),
                property_value.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE Acquire() override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->Acquire();
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_acquire_calls);
            MaybeLogInputSummary();
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8W." + device_name_ + ".Acquire|caller=" + caller_description + "|" +
            DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::Acquire caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE Unacquire() override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->Unacquire();
        if (track_mouse_) {
            g_input_probe.last_mouse_sample_valid = false;
        }
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_unacquire_calls);
            MaybeLogInputSummary();
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8W." + device_name_ + ".Unacquire|caller=" + caller_description + "|" +
            DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::Unacquire caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override {
        void* caller = _ReturnAddress();
        const std::uintptr_t caller_rva = GetCallerRva(caller);
        const HRESULT result = inner_->GetDeviceState(size, data);
        if (track_mouse_) {
            if (SUCCEEDED(result) && data != nullptr && size >= sizeof(DIMOUSESTATE2)) {
                RecordMouseDeviceState(*reinterpret_cast<const DIMOUSESTATE2*>(data));
            } else if (FAILED(result)) {
                g_input_probe.last_mouse_sample_valid = false;
            }
            if (g_supported_game_build && caller_rva == 0x0082798A) {
                MaybeLogHotInputStack("DI8W.Mouse.GetDeviceState");
            }
        }
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_get_device_state_calls);
            MaybeLogInputSummary();
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8W." + device_name_ + ".GetDeviceState|" + std::to_string(size) + "|caller=" +
            caller_description + "|" + DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::GetDeviceState(size=%lu) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                static_cast<unsigned long>(size),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceData(
        DWORD object_data_size,
        LPDIDEVICEOBJECTDATA object_data,
        LPDWORD in_out_count,
        DWORD flags) override {
        void* caller = _ReturnAddress();
        const DWORD requested_count = in_out_count != nullptr ? *in_out_count : 0;
        const HRESULT result =
            inner_->GetDeviceData(object_data_size, object_data, in_out_count, flags);
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_get_device_data_calls);
            MaybeLogInputSummary();
        }

        const DWORD actual_count = in_out_count != nullptr ? *in_out_count : 0;
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8W." + device_name_ + ".GetDeviceData|" + std::to_string(object_data_size) + "|" +
            std::to_string(requested_count) + "|" + std::to_string(actual_count) + "|" +
            FormatHex32(flags) + "|caller=" + caller_description + "|" +
            DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::GetDeviceData(obj=%lu requested=%lu actual=%lu flags=%s) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                static_cast<unsigned long>(object_data_size),
                static_cast<unsigned long>(requested_count),
                static_cast<unsigned long>(actual_count),
                FormatHex32(flags).c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT format) override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->SetDataFormat(format);
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_set_data_format_calls);
            MaybeLogInputSummary();
        }

        const std::string format_description = DescribeDataFormat(format);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8W." + device_name_ + ".SetDataFormat|" + format_description + "|caller=" +
            caller_description + "|" + DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::SetDataFormat(%s) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                format_description.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE event) override {
        return inner_->SetEventNotification(event);
    }

    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD flags) override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->SetCooperativeLevel(window, flags);
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_set_cooperative_level_calls);
            MaybeLogInputSummary();
        }

        const std::string cooperative_flags = DescribeCooperativeFlags(flags);
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8W." + device_name_ + ".SetCooperativeLevel|" + cooperative_flags + "|caller=" +
            caller_description + "|" + DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::SetCooperativeLevel(%s) caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                cooperative_flags.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE GetObjectInfo(
        LPDIDEVICEOBJECTINSTANCEW object,
        DWORD object_id,
        DWORD flags) override {
        return inner_->GetObjectInfo(object, object_id, flags);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW instance) override {
        return inner_->GetDeviceInfo(instance);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND window, DWORD flags) override {
        return inner_->RunControlPanel(window, flags);
    }

    HRESULT STDMETHODCALLTYPE Initialize(
        HINSTANCE instance,
        DWORD version,
        REFGUID guid) override {
        return inner_->Initialize(instance, version, guid);
    }

    HRESULT STDMETHODCALLTYPE CreateEffect(
        REFGUID effect_guid,
        LPCDIEFFECT effect,
        LPDIRECTINPUTEFFECT* out_effect,
        LPUNKNOWN outer) override {
        return inner_->CreateEffect(effect_guid, effect, out_effect, outer);
    }

    HRESULT STDMETHODCALLTYPE EnumEffects(
        LPDIENUMEFFECTSCALLBACKW callback,
        LPVOID context,
        DWORD effect_type) override {
        return inner_->EnumEffects(callback, context, effect_type);
    }

    HRESULT STDMETHODCALLTYPE GetEffectInfo(
        LPDIEFFECTINFOW effect_info,
        REFGUID effect_guid) override {
        return inner_->GetEffectInfo(effect_info, effect_guid);
    }

    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD state) override {
        return inner_->GetForceFeedbackState(state);
    }

    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD flags) override {
        return inner_->SendForceFeedbackCommand(flags);
    }

    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(
        LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback,
        LPVOID context,
        DWORD flags) override {
        return inner_->EnumCreatedEffectObjects(callback, context, flags);
    }

    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE escape) override {
        return inner_->Escape(escape);
    }

    HRESULT STDMETHODCALLTYPE Poll() override {
        void* caller = _ReturnAddress();
        const HRESULT result = inner_->Poll();
        if (!g_config.log_input_traffic) {
            return result;
        }

        if (track_mouse_) {
            InterlockedIncrement(&g_input_probe.mouse_poll_calls);
            MaybeLogInputSummary();
        }

        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "DI8W." + device_name_ + ".Poll|caller=" + caller_description + "|" +
            DescribeDirectInputResult(result);
        if (RememberInputEvent(signature)) {
            Log(
                "%s::Poll caller=%s bytes=%s hr=%s",
                device_name_.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                DescribeDirectInputResult(result).c_str());
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE SendDeviceData(
        DWORD object_data_size,
        LPCDIDEVICEOBJECTDATA object_data,
        LPDWORD in_out_count,
        DWORD flags) override {
        return inner_->SendDeviceData(object_data_size, object_data, in_out_count, flags);
    }

    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(
        LPCWSTR file_name,
        LPDIENUMEFFECTSINFILECALLBACK callback,
        LPVOID context,
        DWORD flags) override {
        return inner_->EnumEffectsInFile(file_name, callback, context, flags);
    }

    HRESULT STDMETHODCALLTYPE WriteEffectToFile(
        LPCWSTR file_name,
        DWORD entries,
        LPDIFILEEFFECT effects,
        DWORD flags) override {
        return inner_->WriteEffectToFile(file_name, entries, effects, flags);
    }

    HRESULT STDMETHODCALLTYPE BuildActionMap(
        LPDIACTIONFORMATW action_format,
        LPCWSTR user_name,
        DWORD flags) override {
        return inner_->BuildActionMap(action_format, user_name, flags);
    }

    HRESULT STDMETHODCALLTYPE SetActionMap(
        LPDIACTIONFORMATW action_format,
        LPCWSTR user_name,
        DWORD flags) override {
        return inner_->SetActionMap(action_format, user_name, flags);
    }

    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW image_info) override {
        return inner_->GetImageInfo(image_info);
    }

private:
    IDirectInputDevice8W* inner_ = nullptr;
    std::string device_name_;
    bool track_mouse_ = false;
};

class DirectInput8AProxy final : public IDirectInput8A {
public:
    explicit DirectInput8AProxy(IDirectInput8A* inner) : inner_(inner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualGuidValue(riid, kIID_IUnknown) ||
            IsEqualGuidValue(riid, kIID_DirectInput8A)) {
            *object = static_cast<IDirectInput8A*>(this);
            AddRef();
            return S_OK;
        }
        return inner_->QueryInterface(riid, object);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return inner_->AddRef();
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG ref_count = inner_->Release();
        if (ref_count == 0) {
            delete this;
        }
        return ref_count;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(
        REFGUID rguid,
        LPDIRECTINPUTDEVICE8A* device,
        LPUNKNOWN outer) override {
        const HRESULT result = inner_->CreateDevice(rguid, device, outer);
        if (g_config.log_input_traffic) {
            const std::string guid = DescribeGuid(rguid);
            if (RememberInputEvent("DI8A.CreateDevice|" + guid)) {
                Log(
                    "DirectInput8A::CreateDevice(%s) hr=%s",
                    guid.c_str(),
                    DescribeDirectInputResult(result).c_str());
            }
        }

        if (SUCCEEDED(result) &&
            device != nullptr &&
            *device != nullptr &&
            ShouldWrapDirectInputDevice(rguid)) {
            const bool track_mouse = IsEqualGuidValue(rguid, kGUID_SysMouse);
            *device = new DirectInputDevice8AProxy(
                *device,
                DescribeGuid(rguid),
                track_mouse);
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(
        DWORD dev_type,
        LPDIENUMDEVICESCALLBACKA callback,
        LPVOID context,
        DWORD flags) override {
        return inner_->EnumDevices(dev_type, callback, context, flags);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID rguid) override {
        return inner_->GetDeviceStatus(rguid);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND window, DWORD flags) override {
        return inner_->RunControlPanel(window, flags);
    }

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version) override {
        return inner_->Initialize(instance, version);
    }

    HRESULT STDMETHODCALLTYPE FindDevice(
        REFGUID rguid,
        LPCSTR name,
        LPGUID guid_instance) override {
        return inner_->FindDevice(rguid, name, guid_instance);
    }

    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(
        LPCSTR user_name,
        LPDIACTIONFORMATA action_format,
        LPDIENUMDEVICESBYSEMANTICSCBA callback,
        LPVOID reference,
        DWORD flags) override {
        return inner_->EnumDevicesBySemantics(
            user_name, action_format, callback, reference, flags);
    }

    HRESULT STDMETHODCALLTYPE ConfigureDevices(
        LPDICONFIGUREDEVICESCALLBACK callback,
        LPDICONFIGUREDEVICESPARAMSA parameters,
        DWORD flags,
        LPVOID reference) override {
        return inner_->ConfigureDevices(callback, parameters, flags, reference);
    }

private:
    IDirectInput8A* inner_ = nullptr;
};

class DirectInput8WProxy final : public IDirectInput8W {
public:
    explicit DirectInput8WProxy(IDirectInput8W* inner) : inner_(inner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualGuidValue(riid, kIID_IUnknown) ||
            IsEqualGuidValue(riid, kIID_DirectInput8W)) {
            *object = static_cast<IDirectInput8W*>(this);
            AddRef();
            return S_OK;
        }
        return inner_->QueryInterface(riid, object);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return inner_->AddRef();
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG ref_count = inner_->Release();
        if (ref_count == 0) {
            delete this;
        }
        return ref_count;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(
        REFGUID rguid,
        LPDIRECTINPUTDEVICE8W* device,
        LPUNKNOWN outer) override {
        const HRESULT result = inner_->CreateDevice(rguid, device, outer);
        if (g_config.log_input_traffic) {
            const std::string guid = DescribeGuid(rguid);
            if (RememberInputEvent("DI8W.CreateDevice|" + guid)) {
                Log(
                    "DirectInput8W::CreateDevice(%s) hr=%s",
                    guid.c_str(),
                    DescribeDirectInputResult(result).c_str());
            }
        }

        if (SUCCEEDED(result) &&
            device != nullptr &&
            *device != nullptr &&
            ShouldWrapDirectInputDevice(rguid)) {
            const bool track_mouse = IsEqualGuidValue(rguid, kGUID_SysMouse);
            *device = new DirectInputDevice8WProxy(
                *device,
                DescribeGuid(rguid),
                track_mouse);
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(
        DWORD dev_type,
        LPDIENUMDEVICESCALLBACKW callback,
        LPVOID context,
        DWORD flags) override {
        return inner_->EnumDevices(dev_type, callback, context, flags);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID rguid) override {
        return inner_->GetDeviceStatus(rguid);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND window, DWORD flags) override {
        return inner_->RunControlPanel(window, flags);
    }

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version) override {
        return inner_->Initialize(instance, version);
    }

    HRESULT STDMETHODCALLTYPE FindDevice(
        REFGUID rguid,
        LPCWSTR name,
        LPGUID guid_instance) override {
        return inner_->FindDevice(rguid, name, guid_instance);
    }

    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(
        LPCWSTR user_name,
        LPDIACTIONFORMATW action_format,
        LPDIENUMDEVICESBYSEMANTICSCBW callback,
        LPVOID reference,
        DWORD flags) override {
        return inner_->EnumDevicesBySemantics(
            user_name, action_format, callback, reference, flags);
    }

    HRESULT STDMETHODCALLTYPE ConfigureDevices(
        LPDICONFIGUREDEVICESCALLBACK callback,
        LPDICONFIGUREDEVICESPARAMSW parameters,
        DWORD flags,
        LPVOID reference) override {
        return inner_->ConfigureDevices(callback, parameters, flags, reference);
    }

private:
    IDirectInput8W* inner_ = nullptr;
};

class Direct3D9ExProxy final : public IDirect3D9Ex {
public:
    explicit Direct3D9ExProxy(IDirect3D9Ex* inner) : inner_(inner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualGuidValue(riid, kIID_IUnknown) ||
            IsEqualGuidValue(riid, kIID_Direct3D9) ||
            IsEqualGuidValue(riid, kIID_Direct3D9Ex)) {
            *object = static_cast<IDirect3D9Ex*>(this);
            AddRef();
            return S_OK;
        }
        return inner_->QueryInterface(riid, object);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return inner_->AddRef();
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG ref_count = inner_->Release();
        if (ref_count == 0) {
            delete this;
        }
        return ref_count;
    }

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* initialize_function) override {
        return inner_->RegisterSoftwareDevice(initialize_function);
    }

    UINT STDMETHODCALLTYPE GetAdapterCount() override {
        return inner_->GetAdapterCount();
    }

    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(
        UINT adapter,
        DWORD flags,
        D3DADAPTER_IDENTIFIER9* identifier) override {
        return inner_->GetAdapterIdentifier(adapter, flags, identifier);
    }

    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override {
        return inner_->GetAdapterModeCount(adapter, format);
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterModes(
        UINT adapter,
        D3DFORMAT format,
        UINT mode,
        D3DDISPLAYMODE* display_mode) override {
        return inner_->EnumAdapterModes(adapter, format, mode, display_mode);
    }

    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(
        UINT adapter,
        D3DDISPLAYMODE* mode) override {
        return inner_->GetAdapterDisplayMode(adapter, mode);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceType(
        UINT adapter,
        D3DDEVTYPE dev_type,
        D3DFORMAT display_format,
        D3DFORMAT back_buffer_format,
        BOOL windowed) override {
        return inner_->CheckDeviceType(
            adapter, dev_type, display_format, back_buffer_format, windowed);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DFORMAT adapter_format,
        DWORD usage,
        D3DRESOURCETYPE resource_type,
        D3DFORMAT check_format) override {
        return inner_->CheckDeviceFormat(
            adapter, device_type, adapter_format, usage, resource_type, check_format);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DFORMAT surface_format,
        BOOL windowed,
        D3DMULTISAMPLE_TYPE multi_sample_type,
        DWORD* quality_levels) override {
        return inner_->CheckDeviceMultiSampleType(
            adapter,
            device_type,
            surface_format,
            windowed,
            multi_sample_type,
            quality_levels);
    }

    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DFORMAT adapter_format,
        D3DFORMAT render_target_format,
        D3DFORMAT depth_stencil_format) override {
        return inner_->CheckDepthStencilMatch(
            adapter,
            device_type,
            adapter_format,
            render_target_format,
            depth_stencil_format);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DFORMAT source_format,
        D3DFORMAT target_format) override {
        return inner_->CheckDeviceFormatConversion(
            adapter, device_type, source_format, target_format);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceCaps(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DCAPS9* caps) override {
        return inner_->GetDeviceCaps(adapter, device_type, caps);
    }

    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override {
        return inner_->GetAdapterMonitor(adapter);
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(
        UINT adapter,
        D3DDEVTYPE device_type,
        HWND focus_window,
        DWORD behavior_flags,
        D3DPRESENT_PARAMETERS* parameters,
        IDirect3DDevice9** device) override {
        const HRESULT result = inner_->CreateDevice(
            adapter,
            device_type,
            focus_window,
            behavior_flags,
            parameters,
            device);
        if (SUCCEEDED(result) && device != nullptr && *device != nullptr) {
            InstallD3D9DeviceHooks(*device);
        }
        if (!g_config.log_d3d9_traffic) {
            return result;
        }
        InterlockedIncrement(&g_d3d_probe.create_device_calls);
        if (FAILED(result)) {
            InterlockedIncrement(&g_d3d_probe.create_device_failures);
        }
        void* caller = _ReturnAddress();
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string parameter_description = DescribePresentParameters(parameters);
        const std::string signature =
            "D3D9.CreateDevice|caller=" + caller_description + "|" +
            parameter_description + "|" + DescribeD3DResult(result);
        if (RememberD3DEvent(signature)) {
            Log(
                "IDirect3D9Ex::CreateDevice caller=%s adapter=%u type=%lu hwnd=%s behavior=%s params=%s hr=%s device=%s",
                caller_description.c_str(),
                static_cast<unsigned int>(adapter),
                static_cast<unsigned long>(device_type),
                DescribeAddress(focus_window).c_str(),
                FormatHex32(behavior_flags).c_str(),
                parameter_description.c_str(),
                DescribeD3DResult(result).c_str(),
                device != nullptr ? DescribeAddress(*device).c_str() : "<null>");
        }
        MaybeLogD3DSummary();
        return result;
    }

    UINT STDMETHODCALLTYPE GetAdapterModeCountEx(
        UINT adapter,
        const D3DDISPLAYMODEFILTER* filter) override {
        return inner_->GetAdapterModeCountEx(adapter, filter);
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(
        UINT adapter,
        const D3DDISPLAYMODEFILTER* filter,
        UINT mode,
        D3DDISPLAYMODEEX* display_mode) override {
        return inner_->EnumAdapterModesEx(adapter, filter, mode, display_mode);
    }

    HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(
        UINT adapter,
        D3DDISPLAYMODEEX* mode,
        D3DDISPLAYROTATION* rotation) override {
        return inner_->GetAdapterDisplayModeEx(adapter, mode, rotation);
    }

    HRESULT STDMETHODCALLTYPE CreateDeviceEx(
        UINT adapter,
        D3DDEVTYPE device_type,
        HWND focus_window,
        DWORD behavior_flags,
        D3DPRESENT_PARAMETERS* parameters,
        D3DDISPLAYMODEEX* fullscreen_display_mode,
        IDirect3DDevice9Ex** device) override {
        const HRESULT result = inner_->CreateDeviceEx(
            adapter,
            device_type,
            focus_window,
            behavior_flags,
            parameters,
            fullscreen_display_mode,
            device);
        if (SUCCEEDED(result) && device != nullptr && *device != nullptr) {
            InstallD3D9DeviceHooks(*device);
        }
        if (!g_config.log_d3d9_traffic) {
            return result;
        }
        InterlockedIncrement(&g_d3d_probe.create_device_calls);
        if (FAILED(result)) {
            InterlockedIncrement(&g_d3d_probe.create_device_failures);
        }
        void* caller = _ReturnAddress();
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string parameter_description = DescribePresentParameters(parameters);
        const std::string fullscreen_description =
            DescribeDisplayModeEx(fullscreen_display_mode);
        const std::string signature =
            "D3D9.CreateDeviceEx|caller=" + caller_description + "|" +
            parameter_description + "|" + fullscreen_description + "|" +
            DescribeD3DResult(result);
        if (RememberD3DEvent(signature)) {
            Log(
                "IDirect3D9Ex::CreateDeviceEx caller=%s adapter=%u type=%lu hwnd=%s behavior=%s params=%s fullscreen=%s hr=%s device=%s",
                caller_description.c_str(),
                static_cast<unsigned int>(adapter),
                static_cast<unsigned long>(device_type),
                DescribeAddress(focus_window).c_str(),
                FormatHex32(behavior_flags).c_str(),
                parameter_description.c_str(),
                fullscreen_description.c_str(),
                DescribeD3DResult(result).c_str(),
                device != nullptr ? DescribeAddress(*device).c_str() : "<null>");
        }
        MaybeLogD3DSummary();
        return result;
    }

    HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT adapter, LUID* luid) override {
        return inner_->GetAdapterLUID(adapter, luid);
    }

private:
    IDirect3D9Ex* inner_ = nullptr;
};

class Direct3D9Proxy final : public IDirect3D9 {
public:
    explicit Direct3D9Proxy(IDirect3D9* inner) : inner_(inner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualGuidValue(riid, kIID_IUnknown) || IsEqualGuidValue(riid, kIID_Direct3D9)) {
            *object = static_cast<IDirect3D9*>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualGuidValue(riid, kIID_Direct3D9Ex)) {
            IDirect3D9Ex* direct3d_ex = nullptr;
            const HRESULT result = inner_->QueryInterface(
                riid,
                reinterpret_cast<void**>(&direct3d_ex));
            if (SUCCEEDED(result) && direct3d_ex != nullptr) {
                *object = static_cast<IDirect3D9Ex*>(new Direct3D9ExProxy(direct3d_ex));
            }
            return result;
        }
        return inner_->QueryInterface(riid, object);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return inner_->AddRef();
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG ref_count = inner_->Release();
        if (ref_count == 0) {
            delete this;
        }
        return ref_count;
    }

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* initialize_function) override {
        return inner_->RegisterSoftwareDevice(initialize_function);
    }

    UINT STDMETHODCALLTYPE GetAdapterCount() override {
        return inner_->GetAdapterCount();
    }

    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(
        UINT adapter,
        DWORD flags,
        D3DADAPTER_IDENTIFIER9* identifier) override {
        return inner_->GetAdapterIdentifier(adapter, flags, identifier);
    }

    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override {
        return inner_->GetAdapterModeCount(adapter, format);
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterModes(
        UINT adapter,
        D3DFORMAT format,
        UINT mode,
        D3DDISPLAYMODE* display_mode) override {
        return inner_->EnumAdapterModes(adapter, format, mode, display_mode);
    }

    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(
        UINT adapter,
        D3DDISPLAYMODE* mode) override {
        return inner_->GetAdapterDisplayMode(adapter, mode);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceType(
        UINT adapter,
        D3DDEVTYPE dev_type,
        D3DFORMAT display_format,
        D3DFORMAT back_buffer_format,
        BOOL windowed) override {
        return inner_->CheckDeviceType(
            adapter, dev_type, display_format, back_buffer_format, windowed);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DFORMAT adapter_format,
        DWORD usage,
        D3DRESOURCETYPE resource_type,
        D3DFORMAT check_format) override {
        return inner_->CheckDeviceFormat(
            adapter, device_type, adapter_format, usage, resource_type, check_format);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DFORMAT surface_format,
        BOOL windowed,
        D3DMULTISAMPLE_TYPE multi_sample_type,
        DWORD* quality_levels) override {
        return inner_->CheckDeviceMultiSampleType(
            adapter,
            device_type,
            surface_format,
            windowed,
            multi_sample_type,
            quality_levels);
    }

    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DFORMAT adapter_format,
        D3DFORMAT render_target_format,
        D3DFORMAT depth_stencil_format) override {
        return inner_->CheckDepthStencilMatch(
            adapter,
            device_type,
            adapter_format,
            render_target_format,
            depth_stencil_format);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DFORMAT source_format,
        D3DFORMAT target_format) override {
        return inner_->CheckDeviceFormatConversion(
            adapter, device_type, source_format, target_format);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceCaps(
        UINT adapter,
        D3DDEVTYPE device_type,
        D3DCAPS9* caps) override {
        return inner_->GetDeviceCaps(adapter, device_type, caps);
    }

    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override {
        return inner_->GetAdapterMonitor(adapter);
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(
        UINT adapter,
        D3DDEVTYPE device_type,
        HWND focus_window,
        DWORD behavior_flags,
        D3DPRESENT_PARAMETERS* parameters,
        IDirect3DDevice9** device) override {
        const HRESULT result = inner_->CreateDevice(
            adapter,
            device_type,
            focus_window,
            behavior_flags,
            parameters,
            device);
        if (SUCCEEDED(result) && device != nullptr && *device != nullptr) {
            InstallD3D9DeviceHooks(*device);
        }
        if (!g_config.log_d3d9_traffic) {
            return result;
        }
        InterlockedIncrement(&g_d3d_probe.create_device_calls);
        if (FAILED(result)) {
            InterlockedIncrement(&g_d3d_probe.create_device_failures);
        }
        void* caller = _ReturnAddress();
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string parameter_description = DescribePresentParameters(parameters);
        const std::string signature =
            "D3D9.CreateDevice|caller=" + caller_description + "|" +
            parameter_description + "|" + DescribeD3DResult(result);
        if (RememberD3DEvent(signature)) {
            Log(
                "IDirect3D9::CreateDevice caller=%s adapter=%u type=%lu hwnd=%s behavior=%s params=%s hr=%s device=%s",
                caller_description.c_str(),
                static_cast<unsigned int>(adapter),
                static_cast<unsigned long>(device_type),
                DescribeAddress(focus_window).c_str(),
                FormatHex32(behavior_flags).c_str(),
                parameter_description.c_str(),
                DescribeD3DResult(result).c_str(),
                device != nullptr ? DescribeAddress(*device).c_str() : "<null>");
        }
        MaybeLogD3DSummary();
        return result;
    }

private:
    IDirect3D9* inner_ = nullptr;
};

void Log(const char* format, ...) {
    char message[1024] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    char line[1200] = {};
    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    std::snprintf(
        line,
        sizeof(line),
        "[%02u:%02u:%02u.%03u] %s\r\n",
        local_time.wHour,
        local_time.wMinute,
        local_time.wSecond,
        local_time.wMilliseconds,
        message);

    OutputDebugStringA(line);

    if (!g_config.enable_log || g_log_path.empty()) {
        return;
    }

    if (g_log_lock_ready) {
        EnterCriticalSection(&g_log_lock);
    }

    HANDLE file = CreateFileW(
        g_log_path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
        CloseHandle(file);
    }

    if (g_log_lock_ready) {
        LeaveCriticalSection(&g_log_lock);
    }
}

bool PatchIAT(
    HMODULE module,
    const char* imported_module,
    const char* imported_name,
    void* replacement,
    void** original_out) {
    if (!module || !imported_module || !imported_name || !replacement) {
        return false;
    }

    auto* base = reinterpret_cast<std::uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const auto& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0) {
        return false;
    }

    auto* imports =
        reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);

    for (; imports->Name != 0; ++imports) {
        const char* dll_name = reinterpret_cast<const char*>(base + imports->Name);
        if (_stricmp(dll_name, imported_module) != 0) {
            continue;
        }

        auto* first_thunk =
            reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
        auto* lookup_thunk = imports->OriginalFirstThunk != 0
            ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk)
            : first_thunk;

        for (; lookup_thunk->u1.AddressOfData != 0; ++lookup_thunk, ++first_thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(lookup_thunk->u1.Ordinal)) {
                continue;
            }

            auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                base + lookup_thunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), imported_name) != 0) {
                continue;
            }

            DWORD old_protect = 0;
            if (!VirtualProtect(
                    &first_thunk->u1.Function,
                    sizeof(first_thunk->u1.Function),
                    PAGE_EXECUTE_READWRITE,
                    &old_protect)) {
                return false;
            }

            if (original_out && *original_out == nullptr) {
                *original_out = reinterpret_cast<void*>(first_thunk->u1.Function);
            }

            first_thunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);

            DWORD ignored = 0;
            VirtualProtect(
                &first_thunk->u1.Function,
                sizeof(first_thunk->u1.Function),
                old_protect,
                &ignored);

            FlushInstructionCache(
                GetCurrentProcess(),
                &first_thunk->u1.Function,
                sizeof(first_thunk->u1.Function));
            return true;
        }
    }

    return false;
}

bool InstallInlineHook(
    void* target,
    void* replacement,
    void** original_out,
    std::size_t stolen_size) {
    if (target == nullptr ||
        replacement == nullptr ||
        original_out == nullptr ||
        stolen_size < 5) {
        return false;
    }

    auto* trampoline = reinterpret_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        stolen_size + 5,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        return false;
    }

    auto* target_bytes = reinterpret_cast<std::uint8_t*>(target);
    std::memcpy(trampoline, target_bytes, stolen_size);

    trampoline[stolen_size] = 0xE9;
    *reinterpret_cast<std::int32_t*>(trampoline + stolen_size + 1) =
        static_cast<std::int32_t>(
            (target_bytes + stolen_size) - (trampoline + stolen_size + 5));

    DWORD old_protect = 0;
    if (!VirtualProtect(target_bytes, stolen_size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    target_bytes[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(target_bytes + 1) = static_cast<std::int32_t>(
        reinterpret_cast<std::uint8_t*>(replacement) - (target_bytes + 5));
    for (std::size_t index = 5; index < stolen_size; ++index) {
        target_bytes[index] = 0x90;
    }

    DWORD ignored = 0;
    VirtualProtect(target_bytes, stolen_size, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target_bytes, stolen_size);
    FlushInstructionCache(GetCurrentProcess(), trampoline, stolen_size + 5);

    *original_out = trampoline;
    return true;
}

void LoadConfig() {
    auto read_int = [](const wchar_t* section, const wchar_t* key, int default_value) -> int {
        wchar_t buffer[64]{};
        constexpr wchar_t kMissing[] = L"__repatch_missing__";
        GetPrivateProfileStringW(
            section,
            key,
            kMissing,
            buffer,
            static_cast<DWORD>(_countof(buffer)),
            g_ini_path.c_str());
        if (lstrcmpW(buffer, kMissing) != 0) {
            return _wtoi(buffer);
        }
        return GetPrivateProfileIntW(L"MAIN", key, default_value, g_ini_path.c_str());
    };
    auto read_bool = [&](const wchar_t* section, const wchar_t* key, bool default_value) -> bool {
        return read_int(section, key, default_value ? 1 : 0) != 0;
    };

    g_config.enable_log = read_bool(L"General", L"EnableLog", true);
    g_config.high_precision_time_get_time =
        read_bool(L"Timing", L"HighPrecisionTimeGetTime", true);
    g_config.precise_short_sleep = read_bool(L"Timing", L"PreciseShortSleep", true);
    int short_sleep_threshold_ms = read_int(L"Timing", L"ShortSleepThresholdMs", 2);
    if (short_sleep_threshold_ms < 0) {
        short_sleep_threshold_ms = 0;
    } else if (short_sleep_threshold_ms > 16) {
        short_sleep_threshold_ms = 16;
    }
    g_config.short_sleep_threshold_ms = static_cast<DWORD>(short_sleep_threshold_ms);
    g_config.crash_dumps = read_bool(L"General", L"CrashDumps", true);
    g_config.protect_unhandled_exception_filter =
        read_bool(L"General", L"ProtectUnhandledExceptionFilter", true);
    g_config.log_ini_traffic = read_bool(L"Diagnostics", L"LogIniTraffic", false);
    g_config.log_input_traffic = read_bool(L"Diagnostics", L"LogInputTraffic", false);
    g_config.log_timing_traffic = read_bool(L"Diagnostics", L"LogTimingTraffic", false);
    g_config.log_d3d9_traffic = read_bool(L"Diagnostics", L"LogD3D9Traffic", false);
    g_config.optimize_worker_state_wait =
        read_bool(L"Timing", L"OptimizeWorkerStateWait", true);
    g_config.optimize_ui_thread_timer_period =
        read_bool(L"Timing", L"OptimizeUiThreadTimerPeriod", true);
    g_config.optimize_thread_join_wait =
        read_bool(L"Timing", L"OptimizeThreadJoinWait", true);
    g_config.optimize_mt_worker_wait = read_bool(L"Timing", L"OptimizeMtWorkerWait", true);
    g_config.optimize_mt_worker_state4_timer_wait =
        read_bool(L"Optional", L"OptimizeMtWorkerState4TimerWait", true);
    g_config.optimize_mt_worker_state9_wait =
        read_bool(L"Optional", L"OptimizeMtWorkerState9Wait", false);
    g_config.optimize_ui_thread_message_wait =
        read_bool(L"Timing", L"OptimizeUiThreadMessageWait", true);
    g_config.optimize_queue_worker_yield =
        read_bool(L"Timing", L"OptimizeQueueWorkerYield", true);
    g_config.optimize_pacing_sleep_precision =
        read_bool(L"Timing", L"OptimizePacingSleepPrecision", true);
    g_config.optimize_legacy_delay_precision =
        read_bool(L"Timing", L"OptimizeLegacyDelayPrecision", true);
    g_config.optimize_stream_retry_backoff =
        read_bool(L"Timing", L"OptimizeStreamRetryBackoff", true);
    g_config.optimize_stream_retry_yield =
        read_bool(L"Optional", L"OptimizeStreamRetryYield", false);
    g_config.dump_hot_input_block = read_bool(L"Diagnostics", L"DumpHotInputBlock", false);
    g_config.dump_wide_input_block = read_bool(L"Diagnostics", L"DumpWideInputBlock", false);
    g_config.optimize_get_cursor_pos_pair =
        read_bool(L"Input", L"OptimizeGetCursorPosPair", true);
    g_config.optimize_idle_get_cursor_pos =
        read_bool(L"Input", L"OptimizeIdleGetCursorPos", true);
    g_config.optimize_screen_to_client_pair =
        read_bool(L"Input", L"OptimizeScreenToClientPair", true);
    g_config.optimize_client_to_screen_origin =
        read_bool(L"Input", L"OptimizeClientToScreenOrigin", true);
    g_config.optimize_get_client_rect_pair =
        read_bool(L"Input", L"OptimizeGetClientRectPair", true);
    g_config.optimize_clip_cursor = read_bool(L"Input", L"OptimizeClipCursor", true);
    g_config.disable_qfps_mouse_ctrl_lag =
        read_bool(L"Input", L"DisableQfpsMouseCtrlLag", true);
}

DWORD WINAPI HookedTimeGetTime() {
    if (!g_timer.ready) {
        return g_original_time_get_time ? g_original_time_get_time() : 0;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    const auto elapsed = static_cast<std::uint64_t>(
        (now.QuadPart - g_timer.start_qpc.QuadPart) * 1000ULL /
        static_cast<std::uint64_t>(g_timer.frequency.QuadPart));

    return g_timer.base_time_get_time + static_cast<DWORD>(elapsed);
}

void PreciseSleepSpin(DWORD milliseconds) {
    if (!g_timer.ready || milliseconds == 0) {
        if (g_original_sleep) {
            g_original_sleep(milliseconds);
        }
        return;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const auto ticks_to_wait = static_cast<LONGLONG>(
        (static_cast<std::uint64_t>(milliseconds) * g_timer.frequency.QuadPart + 999) / 1000);
    const auto target = now.QuadPart + ticks_to_wait;
    bool used_waitable_timer = false;

    if (milliseconds != 0) {
        if (g_precise_sleep_timer.handle == nullptr) {
            if (!g_precise_sleep_timer.attempted_high_res) {
                g_precise_sleep_timer.handle = CreateWaitableTimerExW(
                    nullptr,
                    nullptr,
                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                    TIMER_MODIFY_STATE | SYNCHRONIZE);
                g_precise_sleep_timer.attempted_high_res = true;
            }
            if (g_precise_sleep_timer.handle == nullptr) {
                g_precise_sleep_timer.handle = CreateWaitableTimerW(nullptr, FALSE, nullptr);
            }
        }

        if (g_precise_sleep_timer.handle != nullptr) {
            const auto wait_us = static_cast<std::uint64_t>(milliseconds) * 1000ULL;
            const auto timer_wait_us = wait_us > 200 ? wait_us - 200 : wait_us;
            LARGE_INTEGER due_time{};
            due_time.QuadPart = -static_cast<LONGLONG>(timer_wait_us * 10ULL);
            if (SetWaitableTimer(
                    g_precise_sleep_timer.handle,
                    &due_time,
                    0,
                    nullptr,
                    nullptr,
                    FALSE)) {
                WaitForSingleObjectEx(g_precise_sleep_timer.handle, INFINITE, FALSE);
                used_waitable_timer = true;
            }
        }
    }

    if (g_config.log_timing_traffic) {
        if (used_waitable_timer) {
            InterlockedIncrement(&g_timing_probe.precise_sleep_waitable_calls);
        } else {
            InterlockedIncrement(&g_timing_probe.precise_sleep_spin_only_calls);
        }
    }

    for (;;) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= target) {
            return;
        }

        const auto remaining_ms = static_cast<DWORD>(
            ((target - now.QuadPart) * 1000ULL) /
            static_cast<std::uint64_t>(g_timer.frequency.QuadPart));

        if (remaining_ms > 1 && g_original_sleep) {
            g_original_sleep(0);
        } else {
            SwitchToThread();
        }
    }
}

DWORD GetStreamRetryBackoffMs(std::uintptr_t caller_rva, DWORD* streak_out) {
    if (streak_out != nullptr) {
        *streak_out = 0;
    }

    StreamRetryBackoffEntry* entry = GetStreamRetryBackoffEntry(caller_rva);
    if (entry == nullptr || !g_timer.ready || g_timer.frequency.QuadPart <= 0) {
        return 1;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const auto gap_ticks = static_cast<LONGLONG>(
        (static_cast<std::uint64_t>(kStreamRetryBackoffGapMs) *
             static_cast<std::uint64_t>(g_timer.frequency.QuadPart) +
         999ULL) /
        1000ULL);
    if (entry->last_qpc != 0 && now.QuadPart - entry->last_qpc <= gap_ticks) {
        if (entry->streak != MAXDWORD) {
            ++entry->streak;
        }
    } else {
        entry->streak = 0;
    }
    entry->last_qpc = now.QuadPart;

    if (streak_out != nullptr) {
        *streak_out = entry->streak;
    }

    if (entry->streak >= kStreamRetryBackoffLongThreshold) {
        return kStreamRetryBackoffLongMs;
    }
    if (entry->streak >= kStreamRetryBackoffMediumThreshold) {
        return kStreamRetryBackoffMediumMs;
    }
    return 1;
}

void SleepForStreamRetryBackoff(std::uintptr_t caller_rva, void* caller) {
    DWORD streak = 0;
    const DWORD wait_ms = GetStreamRetryBackoffMs(caller_rva, &streak);
    PreciseSleepSpin(wait_ms);

    if (!g_config.log_timing_traffic) {
        return;
    }

    InterlockedIncrement(&g_timing_probe.stream_retry_backoff_calls);
    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string signature =
        "StreamRetryBackoff|caller=" + caller_description +
        "|wait=" + std::to_string(wait_ms);
    if (RememberTimingEvent(signature)) {
        Log(
            "StreamRetryBackoff caller=%s wait=%lu streak=%lu",
            caller_description.c_str(),
            static_cast<unsigned long>(wait_ms),
            static_cast<unsigned long>(streak));
    }
}

// The x86 hook stub captures preserved registers before calling this dispatcher.
// Several game wait loops keep the worker object pointer in those registers across Sleep(1).
void __cdecl HookedSleepImpl(
    DWORD milliseconds,
    std::uintptr_t ebx_value,
    std::uintptr_t esi_value,
    std::uintptr_t edi_value,
    std::uintptr_t ebp_value,
    void* caller) {
    if (!g_original_sleep) {
        return;
    }

    const std::uintptr_t caller_rva = GetCallerRva(caller);
    const bool use_ui_thread_message_wait =
        g_supported_game_build &&
        g_config.optimize_ui_thread_message_wait &&
        milliseconds == 1 &&
        caller_rva == kUiThreadSleepCallerRva &&
        g_original_msg_wait_for_multiple_objects_ex != nullptr;
    const bool use_queue_worker_yield =
        !use_ui_thread_message_wait &&
        g_supported_game_build &&
        g_config.optimize_queue_worker_yield &&
        milliseconds == 1 &&
        caller_rva == kQueueWorkerYieldSleepCallerRva;
    const bool use_pacing_sleep_precision =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        g_supported_game_build &&
        g_config.optimize_pacing_sleep_precision &&
        caller_rva == kPacingSleepCallerRva &&
        milliseconds != 0 &&
        milliseconds <= kPacingSleepPrecisionMaxMs;
    const bool use_legacy_delay_precision =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        !use_pacing_sleep_precision &&
        g_config.optimize_legacy_delay_precision &&
        IsLegacyDelayPrecisionCaller(caller_rva, milliseconds);
    const bool use_stream_retry_backoff =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        !use_pacing_sleep_precision &&
        !use_legacy_delay_precision &&
        g_config.optimize_stream_retry_backoff &&
        milliseconds == 1 &&
        IsStreamRetrySleepCaller(caller_rva);
    const bool use_thread_join_yield =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        !use_pacing_sleep_precision &&
        !use_legacy_delay_precision &&
        !use_stream_retry_backoff &&
        g_config.optimize_thread_join_wait &&
        g_thread_join_hook_installed &&
        IsThreadJoinSleepCaller(caller_rva);
    const bool use_stream_retry_yield =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        !use_pacing_sleep_precision &&
        !use_legacy_delay_precision &&
        !use_stream_retry_backoff &&
        !use_thread_join_yield &&
        g_config.optimize_stream_retry_yield &&
        milliseconds == 1 &&
        IsStreamRetrySleepCaller(caller_rva);
    const bool use_mt_worker_state4_timer_wait =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        !use_pacing_sleep_precision &&
        !use_legacy_delay_precision &&
        !use_stream_retry_backoff &&
        !use_thread_join_yield &&
        !use_stream_retry_yield &&
        g_supported_game_build &&
        milliseconds == 1 &&
        caller_rva == kMtWorkerSleepCallerRva &&
        TrySleepForMtWorkerState4Timer(ebx_value, esi_value, edi_value, ebp_value, caller);
    const bool use_mt_worker_state9_wait =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        !use_pacing_sleep_precision &&
        !use_legacy_delay_precision &&
        !use_stream_retry_backoff &&
        !use_thread_join_yield &&
        !use_stream_retry_yield &&
        !use_mt_worker_state4_timer_wait &&
        g_supported_game_build &&
        caller_rva == kMtWorkerState9SleepCallerRva &&
        milliseconds == 10 &&
        TryWaitForMtWorkerState9(ebx_value, caller);
    const bool use_mt_worker_wait =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        !use_pacing_sleep_precision &&
        !use_legacy_delay_precision &&
        !use_stream_retry_backoff &&
        !use_thread_join_yield &&
        !use_stream_retry_yield &&
        !use_mt_worker_state4_timer_wait &&
        !use_mt_worker_state9_wait &&
        g_supported_game_build &&
        milliseconds == 1 &&
        caller_rva == kMtWorkerSleepCallerRva &&
        TryWaitForMtWorkerState(ebx_value, esi_value, edi_value, ebp_value, caller);
    const bool use_precise_sleep =
        !use_ui_thread_message_wait &&
        !use_queue_worker_yield &&
        !use_pacing_sleep_precision &&
        !use_legacy_delay_precision &&
        !use_stream_retry_backoff &&
        !use_thread_join_yield &&
        !use_stream_retry_yield &&
        !use_mt_worker_state4_timer_wait &&
        !use_mt_worker_state9_wait &&
        !use_mt_worker_wait &&
        g_config.precise_short_sleep &&
        milliseconds != 0 &&
        milliseconds <= g_config.short_sleep_threshold_ms;

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.sleep_calls);
        if (milliseconds == 0) {
            InterlockedIncrement(&g_timing_probe.sleep_zero_calls);
        }
        if (use_ui_thread_message_wait) {
            InterlockedIncrement(&g_timing_probe.ui_thread_message_wait_calls);
        } else if (use_queue_worker_yield) {
            InterlockedIncrement(&g_timing_probe.queue_worker_yield_calls);
        } else if (use_pacing_sleep_precision) {
            InterlockedIncrement(&g_timing_probe.pacing_sleep_precision_calls);
        } else if (use_legacy_delay_precision) {
            InterlockedIncrement(&g_timing_probe.legacy_delay_precision_calls);
        } else if (use_stream_retry_backoff) {
            // Counted separately in the adaptive stream retry backoff path.
        } else if (use_stream_retry_yield) {
            InterlockedIncrement(&g_timing_probe.stream_retry_yield_calls);
        } else if (use_thread_join_yield) {
            InterlockedIncrement(&g_timing_probe.thread_join_yield_calls);
        } else if (use_mt_worker_state4_timer_wait) {
            // Counted separately in the mt-worker timer wait path.
        } else if (use_mt_worker_state9_wait) {
            // Counted separately in the mt-worker state9 wait path.
        } else if (use_mt_worker_wait) {
            // Counted separately in the mt-worker wait path.
        } else if (use_precise_sleep) {
            InterlockedIncrement(&g_timing_probe.precise_sleep_calls);
        } else {
            InterlockedIncrement(&g_timing_probe.forwarded_sleep_calls);
        }
        g_timing_probe.last_sleep_ms = milliseconds;
        const std::string caller_description = DescribeCallerAddress(caller);
        const char* sleep_mode =
            use_ui_thread_message_wait ? "ui-msgwait" :
            (use_queue_worker_yield ? "queue-yield" :
             (use_pacing_sleep_precision ? "pacing-precise" :
              (use_legacy_delay_precision ? "legacy-precise" :
               (use_stream_retry_backoff ? "stream-backoff" :
                (use_stream_retry_yield ? "stream-yield" :
                 (use_thread_join_yield ? "join-yield" :
                 (use_mt_worker_state4_timer_wait ? "mt-worker-timer" :
                  (use_mt_worker_state9_wait ? "mt-worker-state9" :
                   (use_mt_worker_wait ? "mt-worker" :
                                        (use_precise_sleep ? "precise" : "forward"))))))))));
        const std::string signature =
            "Sleep|ms=" + DescribeWaitTimeout(milliseconds) +
            "|mode=" + std::string(sleep_mode) +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "Sleep(%lu) mode=%s caller=%s bytes=%s",
                static_cast<unsigned long>(milliseconds),
                sleep_mode,
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str());
        }
        if (g_supported_game_build &&
            caller_rva == kMtWorkerSleepCallerRva &&
            !use_mt_worker_state4_timer_wait &&
            !use_mt_worker_wait) {
            MaybeLogMtWorkerHotSleepState(
                ebx_value,
                esi_value,
                edi_value,
                ebp_value,
                caller);
        }
        MaybeLogTimingSummary();
    }

    if (use_ui_thread_message_wait) {
        const DWORD result = g_original_msg_wait_for_multiple_objects_ex(
            0,
            nullptr,
            kUiThreadMessageWaitTimeoutMs,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (result == WAIT_FAILED) {
            PreciseSleepSpin(milliseconds);
        }
        return;
    }

    if (use_queue_worker_yield) {
        if (!SwitchToThread() && g_original_sleep) {
            g_original_sleep(0);
        }
        return;
    }

    if (use_pacing_sleep_precision) {
        PreciseSleepSpin(milliseconds);
        return;
    }

    if (use_legacy_delay_precision) {
        PreciseSleepSpin(milliseconds);
        return;
    }

    if (use_stream_retry_backoff) {
        SleepForStreamRetryBackoff(caller_rva, caller);
        return;
    }

    if (use_stream_retry_yield) {
        if (!SwitchToThread() && g_original_sleep) {
            g_original_sleep(0);
        }
        return;
    }

    if (use_thread_join_yield) {
        if (!SwitchToThread() && g_original_sleep) {
            g_original_sleep(0);
        }
        return;
    }

    if (use_mt_worker_state4_timer_wait) {
        return;
    }

    if (use_mt_worker_state9_wait) {
        return;
    }

    if (use_mt_worker_wait) {
        return;
    }

    if (!use_precise_sleep) {
        g_original_sleep(milliseconds);
        return;
    }

    PreciseSleepSpin(milliseconds);
}

#if defined(_M_IX86)
extern "C" __declspec(naked) VOID WINAPI HookedSleep(DWORD) {
    __asm {
        mov eax, [esp + 4]
        mov ecx, [esp]
        push ecx
        push ebp
        push edi
        push esi
        push ebx
        push eax
        call HookedSleepImpl
        add esp, 24
        ret 4
    }
}
#else
VOID WINAPI HookedSleep(DWORD milliseconds) {
    HookedSleepImpl(milliseconds, 0, 0, 0, 0, _ReturnAddress());
}
#endif

DWORD WINAPI HookedSleepEx(DWORD milliseconds, BOOL alertable) {
    if (!g_original_sleep_ex) {
        return 0;
    }

    void* caller = _ReturnAddress();
    const std::uintptr_t caller_rva = GetCallerRva(caller);
    const bool is_alertable_io_wait =
        alertable && milliseconds == INFINITE && IsAlertableSleepExIoCaller(caller_rva);

    const bool use_precise_sleep =
        !alertable &&
        g_config.precise_short_sleep &&
        milliseconds != 0 &&
        milliseconds <= g_config.short_sleep_threshold_ms;

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.sleep_ex_calls);
        if (alertable) {
            InterlockedIncrement(&g_timing_probe.alertable_sleep_ex_calls);
        }
        if (use_precise_sleep) {
            InterlockedIncrement(&g_timing_probe.precise_sleep_ex_calls);
        } else {
            InterlockedIncrement(&g_timing_probe.forwarded_sleep_ex_calls);
        }
        g_timing_probe.last_sleep_ex_ms = milliseconds;
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "SleepEx|ms=" + DescribeWaitTimeout(milliseconds) +
            "|alertable=" + (alertable ? std::string("1") : std::string("0")) +
            "|mode=" + (use_precise_sleep ? std::string("precise") : std::string("forward")) +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "SleepEx(%lu,%d) mode=%s caller=%s bytes=%s",
                static_cast<unsigned long>(milliseconds),
                alertable ? 1 : 0,
                use_precise_sleep ? "precise" : "forward",
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str());
        }
        MaybeLogTimingSummary();
    }

    LARGE_INTEGER wait_start{};
    if (g_config.log_timing_traffic && is_alertable_io_wait && g_timer.ready) {
        QueryPerformanceCounter(&wait_start);
    }

    if (!use_precise_sleep) {
        const DWORD result = g_original_sleep_ex(milliseconds, alertable);
        if (g_config.log_timing_traffic && is_alertable_io_wait && g_timer.ready) {
            LARGE_INTEGER wait_end{};
            QueryPerformanceCounter(&wait_end);
            const LONG waited_us = QpcDeltaToUs(wait_start.QuadPart, wait_end.QuadPart);
            InterlockedIncrement(&g_timing_probe.alertable_sleep_ex_io_wait_calls);
            InterlockedExchangeAdd(&g_timing_probe.alertable_sleep_ex_io_wait_total_us, waited_us);
            UpdateInterlockedMax(&g_timing_probe.alertable_sleep_ex_io_wait_max_us, waited_us);

            const std::string caller_description = DescribeCallerAddress(caller);
            if (RememberTimingEvent("AlertableSleepExIoWait|" + caller_description)) {
                Log(
                    "AlertableSleepExIoWait caller=%s waited_us=%ld bytes=%s",
                    caller_description.c_str(),
                    waited_us,
                    DescribeCodeWindow(caller).c_str());
            }
            MaybeLogTimingSummary();
        }
        return result;
    }

    PreciseSleepSpin(milliseconds);
    return 0;
}

BOOL WINAPI HookedGetExitCodeThread(HANDLE thread, LPDWORD exit_code) {
    if (!g_original_get_exit_code_thread) {
        return FALSE;
    }

    void* caller = _ReturnAddress();
    const std::uintptr_t caller_rva = GetCallerRva(caller);
    if (g_config.optimize_thread_join_wait &&
        IsOptimizedThreadJoinCaller(caller_rva) &&
        thread != nullptr &&
        g_original_wait_for_single_object != nullptr &&
        g_original_get_thread_id != nullptr) {
#if defined(_M_IX86)
        std::uintptr_t ebx_value = 0;
        std::uintptr_t esi_value = 0;
        std::uintptr_t edi_value = 0;
        std::uintptr_t ebp_value = 0;
        __asm {
            mov ebx_value, ebx
            mov esi_value, esi
            mov edi_value, edi
            mov ebp_value, ebp
        }
        MaybeWakeMtWorkerStateFromOuterRegs(
            ebx_value,
            esi_value,
            edi_value,
            ebp_value,
            caller);
#endif
        const DWORD target_thread_id = g_original_get_thread_id(thread);
        if (target_thread_id != 0 && target_thread_id != GetCurrentThreadId()) {
            const DWORD wait_result = g_original_wait_for_single_object(thread, INFINITE);
            if (wait_result == WAIT_OBJECT_0 && g_config.log_timing_traffic) {
                InterlockedIncrement(&g_timing_probe.thread_join_wait_calls);
                const std::string caller_description = DescribeCallerAddress(caller);
                if (RememberTimingEvent("ThreadJoinWait|" + caller_description)) {
                    Log(
                        "Optimized GetExitCodeThread join caller=%s thread=%s wait=%s",
                        caller_description.c_str(),
                        DescribeAddress(thread).c_str(),
                        DescribeWaitResult(wait_result).c_str());
                }
                MaybeLogTimingSummary();
            }
        }
    }

    return g_original_get_exit_code_thread(thread, exit_code);
}

UINT WINAPI HookedTimeBeginPeriod(UINT period) {
    void* caller = _ReturnAddress();
    const std::uintptr_t caller_rva = GetCallerRva(caller);
    if (g_supported_game_build &&
        g_config.optimize_worker_state_wait &&
        g_config.optimize_ui_thread_timer_period &&
        period == 1 &&
        caller_rva == kUiThreadTimeBeginCallerRva) {
        if (g_config.log_timing_traffic) {
            InterlockedIncrement(&g_timing_probe.suppressed_time_begin_period_calls);
            const std::string caller_description = DescribeCallerAddress(caller);
            if (RememberTimingEvent("timeBeginPeriodSuppressed|" + caller_description)) {
                Log(
                    "Suppressed timeBeginPeriod(%u) caller=%s bytes=%s",
                    static_cast<unsigned int>(period),
                    caller_description.c_str(),
                    DescribeCodeWindow(caller).c_str());
            }
            MaybeLogTimingSummary();
        }
        return 0;
    }

    if (!g_original_time_begin_period) {
        return 97u;
    }

    const UINT result = g_original_time_begin_period(period);
    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.time_begin_period_calls);
        if (result == 0) {
            InterlockedIncrement(&g_timing_probe.timer_period_balance);
        }
        g_timing_probe.last_time_begin_period = period;
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "timeBeginPeriod|period=" + std::to_string(period) +
            "|result=" + std::to_string(result) +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "timeBeginPeriod(%u) result=%u caller=%s bytes=%s",
                static_cast<unsigned int>(period),
                static_cast<unsigned int>(result),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str());
        }
        MaybeLogTimingSummary();
    }
    return result;
}

UINT WINAPI HookedTimeEndPeriod(UINT period) {
    void* caller = _ReturnAddress();
    const std::uintptr_t caller_rva = GetCallerRva(caller);
    if (g_supported_game_build &&
        g_config.optimize_worker_state_wait &&
        g_config.optimize_ui_thread_timer_period &&
        period == 1 &&
        caller_rva == kUiThreadTimeEndCallerRva) {
        if (g_config.log_timing_traffic) {
            InterlockedIncrement(&g_timing_probe.suppressed_time_end_period_calls);
            const std::string caller_description = DescribeCallerAddress(caller);
            if (RememberTimingEvent("timeEndPeriodSuppressed|" + caller_description)) {
                Log(
                    "Suppressed timeEndPeriod(%u) caller=%s bytes=%s",
                    static_cast<unsigned int>(period),
                    caller_description.c_str(),
                    DescribeCodeWindow(caller).c_str());
            }
            MaybeLogTimingSummary();
        }
        return 0;
    }

    if (!g_original_time_end_period) {
        return 97u;
    }

    const UINT result = g_original_time_end_period(period);
    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.time_end_period_calls);
        if (result == 0) {
            InterlockedDecrement(&g_timing_probe.timer_period_balance);
        }
        g_timing_probe.last_time_end_period = period;
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string signature =
            "timeEndPeriod|period=" + std::to_string(period) +
            "|result=" + std::to_string(result) +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "timeEndPeriod(%u) result=%u caller=%s bytes=%s",
                static_cast<unsigned int>(period),
                static_cast<unsigned int>(result),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str());
        }
        MaybeLogTimingSummary();
    }
    return result;
}

DWORD WINAPI HookedWaitForSingleObject(HANDLE handle, DWORD milliseconds) {
    if (!g_original_wait_for_single_object) {
        return WAIT_FAILED;
    }

    const DWORD result = g_original_wait_for_single_object(handle, milliseconds);
    if (g_config.log_timing_traffic) {
        void* caller = _ReturnAddress();
        InterlockedIncrement(&g_timing_probe.wait_for_single_object_calls);
        if (milliseconds == 0) {
            InterlockedIncrement(&g_timing_probe.wait_for_single_object_zero_timeout_calls);
        } else if (milliseconds == INFINITE) {
            InterlockedIncrement(&g_timing_probe.wait_for_single_object_infinite_timeout_calls);
        } else if (milliseconds <= g_config.short_sleep_threshold_ms) {
            InterlockedIncrement(&g_timing_probe.wait_for_single_object_short_timeout_calls);
        }

        if (result == WAIT_TIMEOUT) {
            InterlockedIncrement(&g_timing_probe.wait_for_single_object_timeout_results);
        } else if (result == WAIT_FAILED) {
            InterlockedIncrement(&g_timing_probe.wait_for_single_object_failed_results);
        } else if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + 64) {
            InterlockedIncrement(&g_timing_probe.wait_for_single_object_abandoned_results);
        } else {
            InterlockedIncrement(&g_timing_probe.wait_for_single_object_object_results);
        }

        g_timing_probe.last_wait_for_single_object_timeout = milliseconds;
        g_timing_probe.last_wait_for_single_object_result = result;
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string timeout_description = DescribeWaitTimeout(milliseconds);
        const std::string result_description = DescribeWaitResult(result);
        const std::string signature =
            "WaitForSingleObject|to=" + timeout_description +
            "|result=" + result_description +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "WaitForSingleObject(handle=%08lX, timeout=%s) result=%s caller=%s bytes=%s",
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(handle)),
                timeout_description.c_str(),
                result_description.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str());
        }
        MaybeLogTimingSummary();
    }
    return result;
}

DWORD WINAPI HookedWaitForMultipleObjects(
    DWORD count,
    const HANDLE* handles,
    BOOL wait_all,
    DWORD milliseconds) {
    if (!g_original_wait_for_multiple_objects) {
        return WAIT_FAILED;
    }

    const DWORD result =
        g_original_wait_for_multiple_objects(count, handles, wait_all, milliseconds);
    if (g_config.log_timing_traffic) {
        void* caller = _ReturnAddress();
        InterlockedIncrement(&g_timing_probe.wait_for_multiple_objects_calls);
        if (milliseconds == 0) {
            InterlockedIncrement(&g_timing_probe.wait_for_multiple_objects_zero_timeout_calls);
        } else if (milliseconds == INFINITE) {
            InterlockedIncrement(&g_timing_probe.wait_for_multiple_objects_infinite_timeout_calls);
        } else if (milliseconds <= g_config.short_sleep_threshold_ms) {
            InterlockedIncrement(&g_timing_probe.wait_for_multiple_objects_short_timeout_calls);
        }

        if (result == WAIT_TIMEOUT) {
            InterlockedIncrement(&g_timing_probe.wait_for_multiple_objects_timeout_results);
        } else if (result == WAIT_FAILED) {
            InterlockedIncrement(&g_timing_probe.wait_for_multiple_objects_failed_results);
        } else if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + count) {
            InterlockedIncrement(&g_timing_probe.wait_for_multiple_objects_abandoned_results);
        } else {
            InterlockedIncrement(&g_timing_probe.wait_for_multiple_objects_object_results);
        }

        g_timing_probe.last_wait_for_multiple_objects_count = count;
        g_timing_probe.last_wait_for_multiple_objects_wait_all = wait_all;
        g_timing_probe.last_wait_for_multiple_objects_timeout = milliseconds;
        g_timing_probe.last_wait_for_multiple_objects_result = result;
        const std::string caller_description = DescribeCallerAddress(caller);
        const std::string timeout_description = DescribeWaitTimeout(milliseconds);
        const std::string result_description = DescribeWaitResult(result);
        const std::string signature =
            "WaitForMultipleObjects|count=" + std::to_string(count) +
            "|waitAll=" + (wait_all ? std::string("1") : std::string("0")) +
            "|to=" + timeout_description +
            "|result=" + result_description +
            "|caller=" + caller_description;
        if (RememberTimingEvent(signature)) {
            Log(
                "WaitForMultipleObjects(count=%lu, waitAll=%d, timeout=%s) result=%s caller=%s bytes=%s",
                static_cast<unsigned long>(count),
                wait_all ? 1 : 0,
                timeout_description.c_str(),
                result_description.c_str(),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str());
        }
        MaybeLogTimingSummary();
    }
    return result;
}

void __fastcall HookedWorkerStateDispatch(void* self, void*) {
    auto* original = reinterpret_cast<WorkerStateDispatchFn>(g_original_worker_state_dispatch);
    if (original != nullptr) {
        original(self);
    }

    if (self != nullptr && g_wake_by_address_all != nullptr) {
        if (g_config.log_timing_traffic &&
            RememberTimingEvent("WorkerStateDispatch|" + DescribeAddress(self))) {
            Log("WorkerStateDispatch self=%s", DescribeAddress(self).c_str());
        }
        g_wake_by_address_all(reinterpret_cast<PVOID>(GetWorkerStatePointer(self)));
        if (g_config.log_timing_traffic) {
            InterlockedIncrement(&g_timing_probe.worker_state_wake_calls);
            MaybeLogTimingSummary();
        }
    }
}

void __fastcall HookedWorkerStateRequest3(void* self, void*, void* arg0, void* arg1) {
    if (self == nullptr) {
        return;
    }

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.worker_state_request3_calls);
        if (RememberTimingEvent("WorkerStateRequest3|" + DescribeAddress(self))) {
            Log("WorkerStateRequest3 self=%s arg0=%s arg1=%s",
                DescribeAddress(self).c_str(),
                DescribeAddress(arg0).c_str(),
                DescribeAddress(arg1).c_str());
        }
        MaybeLogTimingSummary();
    }

    WaitForWorkerStateIdle(self);
    *GetWorkerStateArg0Pointer(self) = arg0;
    *GetWorkerStateArg1Pointer(self) = arg1;
    *GetWorkerStatePointer(self) = 3;
    WaitForWorkerStateIdle(self);
}

void __fastcall HookedWorkerStateRequest4(void* self, void*, void* arg0, void* arg2) {
    if (self == nullptr) {
        return;
    }

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.worker_state_request4_calls);
        if (RememberTimingEvent("WorkerStateRequest4|" + DescribeAddress(self))) {
            Log("WorkerStateRequest4 self=%s arg0=%s arg2=%s",
                DescribeAddress(self).c_str(),
                DescribeAddress(arg0).c_str(),
                DescribeAddress(arg2).c_str());
        }
        MaybeLogTimingSummary();
    }

    WaitForWorkerStateIdle(self);
    *GetWorkerStateArg0Pointer(self) = arg0;
    *GetWorkerStateArg2Pointer(self) = arg2;
    *GetWorkerStatePointer(self) = 4;
    WaitForWorkerStateIdle(self);
}

int __fastcall HookedWorkerStateRequest5(void* self, void*, void* arg0) {
    if (self == nullptr) {
        return 0;
    }

    if (g_config.log_timing_traffic) {
        InterlockedIncrement(&g_timing_probe.worker_state_request5_calls);
        if (RememberTimingEvent("WorkerStateRequest5|" + DescribeAddress(self))) {
            Log("WorkerStateRequest5 self=%s arg0=%s",
                DescribeAddress(self).c_str(),
                DescribeAddress(arg0).c_str());
        }
        MaybeLogTimingSummary();
    }

    WaitForWorkerStateIdle(self);
    *GetWorkerStateArg0Pointer(self) = arg0;
    *GetWorkerStatePointer(self) = 5;
    WaitForWorkerStateIdle(self);
    return *GetWorkerStateResultPointer(self);
}

bool __fastcall HookedMtWorkerRequestState4(void* self, void*, void* arg0) {
    bool result = false;
    auto* original = reinterpret_cast<MtWorkerRequestState4Fn>(g_original_mt_worker_request_state4);
    if (original != nullptr) {
        result = original(self, arg0);
    }

    WakeMtWorkerState(self, "MtWorkerState4Wake");
    return result;
}

void __fastcall HookedMtWorkerRequestState5(void* self, void*) {
    auto* original = reinterpret_cast<MtWorkerRequestState5Fn>(g_original_mt_worker_request_state5);
    if (original != nullptr) {
        original(self);
    }

    WakeMtWorkerState(self, "MtWorkerState5Wake");
}

void __fastcall HookedMtWorkerRequestState8(void* self, void*) {
    auto* original = reinterpret_cast<MtWorkerRequestState8Fn>(g_original_mt_worker_request_state8);
    if (original != nullptr) {
        original(self);
    }

    WakeMtWorkerState(self, "MtWorkerState8Wake");
}

BOOL WINAPI HookedGetCursorPos(LPPOINT point) {
    if (!g_original_get_cursor_pos) {
        return FALSE;
    }

    void* caller = _ReturnAddress();
    const std::uintptr_t caller_rva = GetCallerRva(caller);
    if (IsOptimizedGetCursorPosCaller(caller_rva)) {
        MaybeDumpHotInputBlock();
        MaybeDumpWideInputBlock();
        MaybeDumpRuntimeQfpsPointerRefs();
        MaybeDumpRuntimeQfpsCallChainBlocks();
        MaybeLogRuntimeQfpsLiveObjects();
        MaybeApplyQfpsMouseCtrlLagPatch(caller_rva);
        if (caller_rva == kGetCursorPosCallerA) {
            MaybeLogHotInputStack("GetCursorPos.A");
        } else {
            MaybeLogHotInputStack("GetCursorPos.B");
        }
    }

    if (point != nullptr &&
        g_config.optimize_get_cursor_pos_pair &&
        IsOptimizedGetCursorPosCaller(caller_rva) &&
        g_input_probe.cached_get_cursor_valid &&
        g_input_probe.cached_get_cursor_thread_id == GetCurrentThreadId() &&
        g_input_probe.cached_get_cursor_rva != 0 &&
        g_input_probe.cached_get_cursor_rva != caller_rva &&
        g_timer.ready) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const auto elapsed_us =
            static_cast<std::uint64_t>(now.QuadPart - g_input_probe.cached_get_cursor_qpc) *
            1000000ULL /
            static_cast<std::uint64_t>(g_timer.frequency.QuadPart);
        if (elapsed_us <= 2000) {
            *point = g_input_probe.cached_get_cursor_pos;
            InterlockedIncrement(&g_input_probe.get_cursor_pos_calls);
            InterlockedIncrement(&g_input_probe.cached_get_cursor_pos_calls);
            g_input_probe.last_cursor_pos = *point;
            g_input_probe.last_cursor_valid = true;
            if (g_config.log_input_traffic) {
                const std::string caller_description = DescribeCallerAddress(caller);
                const std::string signature = "GetCursorPosCached|caller=" + caller_description;
                if (RememberInputEvent(signature)) {
                    Log(
                        "GetCursorPos cached caller=%s source_rva=%08lX value=(%ld,%ld)",
                        caller_description.c_str(),
                        static_cast<unsigned long>(g_input_probe.cached_get_cursor_rva),
                        static_cast<long>(point->x),
                        static_cast<long>(point->y));
                }
                MaybeLogInputSummary();
            }
            return TRUE;
        }
    }

    if (point != nullptr && CanUseIdleGetCursorPosCache(caller_rva)) {
        *point = g_input_probe.cached_get_cursor_pos;
        InterlockedIncrement(&g_input_probe.get_cursor_pos_calls);
        InterlockedIncrement(&g_input_probe.cached_get_cursor_pos_calls);
        InterlockedIncrement(&g_input_probe.idle_cached_get_cursor_pos_calls);
        g_input_probe.last_cursor_pos = *point;
        g_input_probe.last_cursor_valid = true;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        g_input_probe.cached_get_cursor_thread_id = GetCurrentThreadId();
        g_input_probe.cached_get_cursor_rva = caller_rva;
        g_input_probe.cached_get_cursor_qpc = now.QuadPart;
        if (g_config.log_input_traffic) {
            const std::string caller_description = DescribeCallerAddress(caller);
            const std::string signature = "GetCursorPosIdle|caller=" + caller_description;
            if (RememberInputEvent(signature)) {
                Log(
                    "GetCursorPos idle-bypassed caller=%s value=(%ld,%ld) raw=(%ld,%ld,%ld)",
                    caller_description.c_str(),
                    static_cast<long>(point->x),
                    static_cast<long>(point->y),
                    static_cast<long>(g_input_probe.last_mouse_dx),
                    static_cast<long>(g_input_probe.last_mouse_dy),
                    static_cast<long>(g_input_probe.last_mouse_dz));
            }
            MaybeLogInputSummary();
        }
        return TRUE;
    }

    const BOOL result = g_original_get_cursor_pos(point);
    if (result && point != nullptr) {
        InterlockedIncrement(&g_input_probe.get_cursor_pos_calls);
        InterlockedIncrement(&g_input_probe.forwarded_get_cursor_pos_calls);
        g_input_probe.last_cursor_pos = *point;
        g_input_probe.last_cursor_valid = true;
        if (g_config.log_input_traffic) {
            const std::string caller_description = DescribeCallerAddress(caller);
            if (RememberInputEvent("GetCursorPos|caller=" + caller_description)) {
                Log(
                    "GetCursorPos caller=%s bytes=%s result=%d",
                    caller_description.c_str(),
                    DescribeCodeWindow(caller).c_str(),
                    result ? 1 : 0);
            }
            MaybeLogInputSummary();
        }
    }

    if (result && point != nullptr && IsOptimizedGetCursorPosCaller(caller_rva) && g_timer.ready) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        g_input_probe.cached_get_cursor_pos = *point;
        g_input_probe.cached_get_cursor_valid = true;
        g_input_probe.cached_get_cursor_thread_id = GetCurrentThreadId();
        g_input_probe.cached_get_cursor_rva = caller_rva;
        g_input_probe.cached_get_cursor_qpc = now.QuadPart;
        g_input_probe.last_real_get_cursor_qpc = now.QuadPart;
    }
    return result;
}

BOOL WINAPI HookedScreenToClient(HWND window, LPPOINT point) {
    if (!g_original_screen_to_client) {
        return FALSE;
    }

    void* caller = _ReturnAddress();
    const std::uintptr_t caller_rva = GetCallerRva(caller);
    if (IsOptimizedScreenToClientCaller(caller_rva)) {
        MaybeDumpHotInputBlock();
        MaybeDumpWideInputBlock();
        MaybeDumpRuntimeQfpsPointerRefs();
        MaybeDumpRuntimeQfpsCallChainBlocks();
        MaybeLogRuntimeQfpsLiveObjects();
    }
    if (point != nullptr) {
        InterlockedIncrement(&g_input_probe.screen_to_client_calls);
    }

    if (window != nullptr &&
        point != nullptr &&
        g_config.optimize_screen_to_client_pair &&
        IsOptimizedScreenToClientCaller(caller_rva) &&
        IsIdentityScreenToClientWindow(window)) {
        InterlockedIncrement(&g_input_probe.identity_screen_to_client_calls);
        if (g_config.log_input_traffic) {
            const std::string caller_description = DescribeCallerAddress(caller);
            const std::string signature =
                "ScreenToClientIdentity|caller=" + caller_description;
            if (RememberInputEvent(signature)) {
                Log(
                    "ScreenToClient identity-bypassed caller=%s hwnd=%08lX value=(%ld,%ld)",
                    caller_description.c_str(),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)),
                    static_cast<long>(point->x),
                    static_cast<long>(point->y));
            }
            MaybeLogInputSummary();
        }
        return TRUE;
    }

    if (window != nullptr &&
        point != nullptr &&
        g_config.optimize_screen_to_client_pair &&
        IsOptimizedScreenToClientCaller(caller_rva) &&
        g_input_probe.cached_screen_to_client_valid &&
        g_input_probe.cached_screen_to_client_thread_id == GetCurrentThreadId() &&
        g_input_probe.cached_screen_to_client_rva != 0 &&
        g_input_probe.cached_screen_to_client_rva != caller_rva &&
        g_input_probe.cached_screen_to_client_hwnd == window &&
        g_input_probe.cached_screen_to_client_in.x == point->x &&
        g_input_probe.cached_screen_to_client_in.y == point->y &&
        g_timer.ready) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const auto elapsed_us =
            static_cast<std::uint64_t>(now.QuadPart - g_input_probe.cached_screen_to_client_qpc) *
            1000000ULL /
            static_cast<std::uint64_t>(g_timer.frequency.QuadPart);
        if (elapsed_us <= 2000) {
            *point = g_input_probe.cached_screen_to_client_out;
            InterlockedIncrement(&g_input_probe.cached_screen_to_client_calls);
            if (g_config.log_input_traffic) {
                const std::string caller_description = DescribeCallerAddress(caller);
                const std::string signature =
                    "ScreenToClientCached|caller=" + caller_description;
                if (RememberInputEvent(signature)) {
                    Log(
                        "ScreenToClient cached caller=%s source_rva=%08lX hwnd=%08lX value=(%ld,%ld)",
                        caller_description.c_str(),
                        static_cast<unsigned long>(g_input_probe.cached_screen_to_client_rva),
                        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)),
                        static_cast<long>(point->x),
                        static_cast<long>(point->y));
                }
                MaybeLogInputSummary();
            }
            return TRUE;
        }
    }

    POINT original_point{};
    if (point != nullptr) {
        original_point = *point;
    }

    const BOOL result = g_original_screen_to_client(window, point);
    if (result && point != nullptr) {
        InterlockedIncrement(&g_input_probe.forwarded_screen_to_client_calls);
        if (g_config.log_input_traffic) {
            const std::string caller_description = DescribeCallerAddress(caller);
            if (RememberInputEvent("ScreenToClient|caller=" + caller_description)) {
                Log(
                    "ScreenToClient caller=%s hwnd=%08lX in=(%ld,%ld) out=(%ld,%ld) result=%d",
                    caller_description.c_str(),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)),
                    static_cast<long>(original_point.x),
                    static_cast<long>(original_point.y),
                    static_cast<long>(point->x),
                    static_cast<long>(point->y),
                    result ? 1 : 0);
            }
            MaybeLogInputSummary();
        }
    }

    if (result &&
        point != nullptr &&
        window != nullptr &&
        IsOptimizedScreenToClientCaller(caller_rva) &&
        g_timer.ready) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        g_input_probe.cached_screen_to_client_in = original_point;
        g_input_probe.cached_screen_to_client_out = *point;
        g_input_probe.cached_screen_to_client_valid = true;
        g_input_probe.cached_screen_to_client_hwnd = window;
        g_input_probe.cached_screen_to_client_thread_id = GetCurrentThreadId();
        g_input_probe.cached_screen_to_client_rva = caller_rva;
        g_input_probe.cached_screen_to_client_qpc = now.QuadPart;
    }

    return result;
}

BOOL WINAPI HookedClientToScreen(HWND window, LPPOINT point) {
    if (!g_original_client_to_screen) {
        return FALSE;
    }

    void* caller = _ReturnAddress();
    const std::uintptr_t caller_rva = GetCallerRva(caller);
    if (IsOptimizedClientToScreenCaller(caller_rva)) {
        MaybeDumpHotInputBlock();
        MaybeDumpWideInputBlock();
        MaybeDumpRuntimeQfpsPointerRefs();
        MaybeDumpRuntimeQfpsCallChainBlocks();
        MaybeLogRuntimeQfpsLiveObjects();
    }
    if (point != nullptr) {
        InterlockedIncrement(&g_input_probe.client_to_screen_calls);
    }

    POINT original_point{};
    if (point != nullptr) {
        original_point = *point;
    }

    if (window != nullptr &&
        point != nullptr &&
        g_config.optimize_client_to_screen_origin &&
        IsOptimizedClientToScreenCaller(caller_rva) &&
        original_point.x == 0 &&
        original_point.y == 0 &&
        IsIdentityScreenToClientWindow(window)) {
        InterlockedIncrement(&g_input_probe.identity_client_to_screen_calls);
        if (g_config.log_input_traffic) {
            const std::string caller_description = DescribeCallerAddress(caller);
            const std::string signature = "ClientToScreenIdentity|caller=" + caller_description;
            if (RememberInputEvent(signature)) {
                Log(
                    "ClientToScreen identity-bypassed caller=%s hwnd=%08lX value=(%ld,%ld)",
                    caller_description.c_str(),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)),
                    static_cast<long>(point->x),
                    static_cast<long>(point->y));
            }
            MaybeLogInputSummary();
        }
        return TRUE;
    }

    const BOOL result = g_original_client_to_screen(window, point);
    if (result && point != nullptr) {
        InterlockedIncrement(&g_input_probe.forwarded_client_to_screen_calls);
    }

    if (!g_config.log_input_traffic || point == nullptr) {
        return result;
    }

    MaybeLogInputSummary();

    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string signature = "ClientToScreen|caller=" + caller_description;
    if (RememberInputEvent(signature)) {
        Log(
            "ClientToScreen caller=%s hwnd=%08lX in=(%ld,%ld) out=(%ld,%ld) result=%d bytes=%s",
            caller_description.c_str(),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)),
            static_cast<long>(original_point.x),
            static_cast<long>(original_point.y),
            static_cast<long>(point->x),
            static_cast<long>(point->y),
            result ? 1 : 0,
            DescribeCodeWindow(caller).c_str());
    }
    return result;
}

BOOL WINAPI HookedGetClientRect(HWND window, LPRECT rect) {
    if (!g_original_get_client_rect) {
        return FALSE;
    }

    void* caller = _ReturnAddress();
    const std::uintptr_t caller_rva = GetCallerRva(caller);
    if (IsOptimizedGetClientRectCaller(caller_rva)) {
        MaybeDumpHotInputBlock();
        MaybeDumpWideInputBlock();
        MaybeDumpRuntimeQfpsPointerRefs();
        MaybeDumpRuntimeQfpsCallChainBlocks();
        MaybeLogRuntimeQfpsLiveObjects();
    }
    if (rect != nullptr) {
        InterlockedIncrement(&g_input_probe.get_client_rect_calls);
    }

    if (window != nullptr &&
        rect != nullptr &&
        g_config.optimize_get_client_rect_pair &&
        IsOptimizedGetClientRectCaller(caller_rva) &&
        g_input_probe.cached_get_client_rect_valid &&
        g_input_probe.cached_get_client_rect_thread_id == GetCurrentThreadId() &&
        g_input_probe.cached_get_client_rect_hwnd == window &&
        g_timer.ready) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const auto elapsed_us =
            static_cast<std::uint64_t>(now.QuadPart - g_input_probe.cached_get_client_rect_qpc) *
            1000000ULL /
            static_cast<std::uint64_t>(g_timer.frequency.QuadPart);
        if (g_input_probe.cached_get_client_rect_rva != 0 &&
            g_input_probe.cached_get_client_rect_rva != caller_rva &&
            elapsed_us <= 2000) {
            *rect = g_input_probe.cached_get_client_rect;
            InterlockedIncrement(&g_input_probe.cached_get_client_rect_calls);
            if (g_config.log_input_traffic) {
                const std::string caller_description = DescribeCallerAddress(caller);
                const std::string signature = "GetClientRectCached|caller=" + caller_description;
                if (RememberInputEvent(signature)) {
                    Log(
                        "GetClientRect cached caller=%s source_rva=%08lX hwnd=%08lX rect=[%ld,%ld,%ld,%ld]",
                        caller_description.c_str(),
                        static_cast<unsigned long>(g_input_probe.cached_get_client_rect_rva),
                        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)),
                        static_cast<long>(rect->left),
                        static_cast<long>(rect->top),
                        static_cast<long>(rect->right),
                        static_cast<long>(rect->bottom));
                }
                MaybeLogInputSummary();
            }
            return TRUE;
        }

        if (elapsed_us <= kGetClientRectStableRefreshUs &&
            IsIdentityScreenToClientWindow(window)) {
            *rect = g_input_probe.cached_get_client_rect;
            InterlockedIncrement(&g_input_probe.cached_get_client_rect_calls);
            if (g_config.log_input_traffic) {
                const std::string caller_description = DescribeCallerAddress(caller);
                const std::string signature =
                    "GetClientRectStable|caller=" + caller_description;
                if (RememberInputEvent(signature)) {
                    Log(
                        "GetClientRect stable-cache caller=%s hwnd=%08lX age_us=%llu rect=[%ld,%ld,%ld,%ld]",
                        caller_description.c_str(),
                        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)),
                        static_cast<unsigned long long>(elapsed_us),
                        static_cast<long>(rect->left),
                        static_cast<long>(rect->top),
                        static_cast<long>(rect->right),
                        static_cast<long>(rect->bottom));
                }
                MaybeLogInputSummary();
            }
            return TRUE;
        }
    }

    const BOOL result = g_original_get_client_rect(window, rect);
    if (result && rect != nullptr) {
        InterlockedIncrement(&g_input_probe.forwarded_get_client_rect_calls);
    }

    if (result &&
        window != nullptr &&
        rect != nullptr &&
        g_config.optimize_get_client_rect_pair &&
        IsOptimizedGetClientRectCaller(caller_rva) &&
        g_timer.ready) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        g_input_probe.cached_get_client_rect = *rect;
        g_input_probe.cached_get_client_rect_valid = true;
        g_input_probe.cached_get_client_rect_hwnd = window;
        g_input_probe.cached_get_client_rect_thread_id = GetCurrentThreadId();
        g_input_probe.cached_get_client_rect_rva = caller_rva;
        g_input_probe.cached_get_client_rect_qpc = now.QuadPart;
    }

    if (!g_config.log_input_traffic || rect == nullptr) {
        return result;
    }

    MaybeLogInputSummary();

    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string signature = "GetClientRect|caller=" + caller_description;
    if (RememberInputEvent(signature)) {
        Log(
            "GetClientRect caller=%s hwnd=%08lX rect=[%ld,%ld,%ld,%ld] result=%d bytes=%s",
            caller_description.c_str(),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(window)),
            static_cast<long>(rect->left),
            static_cast<long>(rect->top),
            static_cast<long>(rect->right),
            static_cast<long>(rect->bottom),
            result ? 1 : 0,
            DescribeCodeWindow(caller).c_str());
    }
    return result;
}

BOOL WINAPI HookedClipCursor(const RECT* rect) {
    if (!g_original_clip_cursor) {
        return FALSE;
    }

    InterlockedIncrement(&g_input_probe.clip_cursor_calls);
    void* caller = _ReturnAddress();

    bool redundant = false;
    if (g_config.optimize_clip_cursor) {
        if (rect == nullptr) {
            redundant = g_input_probe.last_clip_valid && g_input_probe.last_clip_null;
        } else if (g_input_probe.last_clip_valid && !g_input_probe.last_clip_null) {
            redundant = std::memcmp(rect, &g_input_probe.last_clip_rect, sizeof(RECT)) == 0;
        }
    }

    if (redundant) {
        InterlockedIncrement(&g_input_probe.skipped_clip_cursor_calls);
        MaybeLogInputSummary();
        return TRUE;
    }

    const BOOL result = g_original_clip_cursor(rect);
    if (result) {
        g_input_probe.last_clip_valid = true;
        g_input_probe.last_clip_null = rect == nullptr;
        if (rect != nullptr) {
            g_input_probe.last_clip_rect = *rect;
        }
    }

    if (!g_config.log_input_traffic) {
        return result;
    }

    MaybeLogInputSummary();

    const std::string caller_description = DescribeCallerAddress(caller);
    std::string signature = "ClipCursor|null";
    if (rect != nullptr) {
        char buffer[128] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "ClipCursor|%ld,%ld,%ld,%ld",
            static_cast<long>(rect->left),
            static_cast<long>(rect->top),
            static_cast<long>(rect->right),
            static_cast<long>(rect->bottom));
        signature = buffer;
    }
    signature += "|caller=" + caller_description;

    if (RememberInputEvent(signature)) {
        if (rect == nullptr) {
            Log(
                "ClipCursor(null) caller=%s bytes=%s result=%d",
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                result ? 1 : 0);
        } else {
            Log(
                "ClipCursor([%ld,%ld,%ld,%ld]) caller=%s bytes=%s result=%d",
                static_cast<long>(rect->left),
                static_cast<long>(rect->top),
                static_cast<long>(rect->right),
                static_cast<long>(rect->bottom),
                caller_description.c_str(),
                DescribeCodeWindow(caller).c_str(),
                result ? 1 : 0);
        }
    }

    return result;
}

HRESULT STDMETHODCALLTYPE HookedD3DDevice9TestCooperativeLevel(IDirect3DDevice9* self) {
    if (g_original_d3d9_test_cooperative_level == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    const HRESULT result = g_original_d3d9_test_cooperative_level(self);
    if (!g_config.log_d3d9_traffic) {
        return result;
    }

    InterlockedIncrement(&g_d3d_probe.test_cooperative_level_calls);
    if (FAILED(result)) {
        InterlockedIncrement(&g_d3d_probe.test_cooperative_level_failures);
    }

    void* caller = _ReturnAddress();
    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string signature =
        "D3D9.Device.TestCooperativeLevel|caller=" + caller_description + "|" +
        DescribeD3DResult(result);
    if (RememberD3DEvent(signature)) {
        Log(
            "IDirect3DDevice9::TestCooperativeLevel caller=%s bytes=%s hr=%s",
            caller_description.c_str(),
            DescribeCodeWindow(caller).c_str(),
            DescribeD3DResult(result).c_str());
    }
    MaybeLogD3DSummary();
    return result;
}

HRESULT STDMETHODCALLTYPE HookedD3DDevice9CreateAdditionalSwapChain(
    IDirect3DDevice9* self,
    D3DPRESENT_PARAMETERS* parameters,
    IDirect3DSwapChain9** swap_chain) {
    if (g_original_d3d9_create_additional_swap_chain == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    const HRESULT result = g_original_d3d9_create_additional_swap_chain(
        self,
        parameters,
        swap_chain);
    if (!g_config.log_d3d9_traffic) {
        return result;
    }

    InterlockedIncrement(&g_d3d_probe.create_additional_swap_chain_calls);
    if (FAILED(result)) {
        InterlockedIncrement(&g_d3d_probe.create_additional_swap_chain_failures);
    }

    void* caller = _ReturnAddress();
    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string parameter_description = DescribePresentParameters(parameters);
    const std::string signature =
        "D3D9.Device.CreateAdditionalSwapChain|caller=" + caller_description + "|" +
        parameter_description + "|" + DescribeD3DResult(result);
    if (RememberD3DEvent(signature)) {
        Log(
            "IDirect3DDevice9::CreateAdditionalSwapChain caller=%s params=%s hr=%s swap=%s",
            caller_description.c_str(),
            parameter_description.c_str(),
            DescribeD3DResult(result).c_str(),
            swap_chain != nullptr ? DescribeAddress(*swap_chain).c_str() : "<null>");
    }
    MaybeLogD3DSummary();
    return result;
}

HRESULT STDMETHODCALLTYPE HookedD3DDevice9Reset(
    IDirect3DDevice9* self,
    D3DPRESENT_PARAMETERS* parameters) {
    if (g_original_d3d9_reset == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    const HRESULT result = g_original_d3d9_reset(self, parameters);
    if (!g_config.log_d3d9_traffic) {
        return result;
    }

    InterlockedIncrement(&g_d3d_probe.reset_calls);
    if (FAILED(result)) {
        InterlockedIncrement(&g_d3d_probe.reset_failures);
    }

    void* caller = _ReturnAddress();
    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string parameter_description = DescribePresentParameters(parameters);
    const std::string signature =
        "D3D9.Device.Reset|caller=" + caller_description + "|" +
        parameter_description + "|" + DescribeD3DResult(result);
    if (RememberD3DEvent(signature)) {
        Log(
            "IDirect3DDevice9::Reset caller=%s params=%s hr=%s",
            caller_description.c_str(),
            parameter_description.c_str(),
            DescribeD3DResult(result).c_str());
    }
    MaybeLogD3DSummary();
    return result;
}

HRESULT STDMETHODCALLTYPE HookedD3DDevice9Present(
    IDirect3DDevice9* self,
    const RECT* source_rect,
    const RECT* dest_rect,
    HWND override_window,
    const RGNDATA* dirty_region) {
    if (g_original_d3d9_present == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    const HRESULT result = g_original_d3d9_present(
        self,
        source_rect,
        dest_rect,
        override_window,
        dirty_region);
    if (!g_config.log_d3d9_traffic) {
        return result;
    }

    InterlockedIncrement(&g_d3d_probe.present_calls);
    if (FAILED(result)) {
        InterlockedIncrement(&g_d3d_probe.present_failures);
    }

    void* caller = _ReturnAddress();
    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string signature =
        "D3D9.Device.Present|caller=" + caller_description + "|" + DescribeD3DResult(result);
    if (RememberD3DEvent(signature)) {
        Log(
            "IDirect3DDevice9::Present caller=%s src=%s dst=%s hwnd=%s dirty=%s hr=%s",
            caller_description.c_str(),
            DescribeAddress(source_rect).c_str(),
            DescribeAddress(dest_rect).c_str(),
            DescribeAddress(override_window).c_str(),
            DescribeAddress(dirty_region).c_str(),
            DescribeD3DResult(result).c_str());
    }
    MaybeLogD3DSummary();
    return result;
}

bool InstallD3D9DeviceHooks(IDirect3DDevice9* device) {
    if (device == nullptr) {
        return false;
    }

    bool installed_any = false;

    if (PatchVtableEntry(
            device,
            kD3D9DeviceTestCooperativeLevelVtableSlot,
            reinterpret_cast<void*>(HookedD3DDevice9TestCooperativeLevel),
            reinterpret_cast<void**>(&g_original_d3d9_test_cooperative_level))) {
        installed_any = true;
    }

    if (PatchVtableEntry(
            device,
            kD3D9DeviceCreateAdditionalSwapChainVtableSlot,
            reinterpret_cast<void*>(HookedD3DDevice9CreateAdditionalSwapChain),
            reinterpret_cast<void**>(&g_original_d3d9_create_additional_swap_chain))) {
        installed_any = true;
    }

    if (PatchVtableEntry(
            device,
            kD3D9DeviceResetVtableSlot,
            reinterpret_cast<void*>(HookedD3DDevice9Reset),
            reinterpret_cast<void**>(&g_original_d3d9_reset))) {
        installed_any = true;
    }

    if (PatchVtableEntry(
            device,
            kD3D9DevicePresentVtableSlot,
            reinterpret_cast<void*>(HookedD3DDevice9Present),
            reinterpret_cast<void**>(&g_original_d3d9_present))) {
        installed_any = true;
    }

    return installed_any;
}

HRESULT STDMETHODCALLTYPE HookedD3D9CreateDevice(
    IDirect3D9* self,
    UINT adapter,
    D3DDEVTYPE device_type,
    HWND focus_window,
    DWORD behavior_flags,
    D3DPRESENT_PARAMETERS* parameters,
    IDirect3DDevice9** device) {
    if (g_original_d3d9_create_device == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    const HRESULT result = g_original_d3d9_create_device(
        self,
        adapter,
        device_type,
        focus_window,
        behavior_flags,
        parameters,
        device);

    if (SUCCEEDED(result) && device != nullptr && *device != nullptr) {
        InstallD3D9DeviceHooks(*device);
    }

    if (!g_config.log_d3d9_traffic) {
        return result;
    }

    InterlockedIncrement(&g_d3d_probe.create_device_calls);
    if (FAILED(result)) {
        InterlockedIncrement(&g_d3d_probe.create_device_failures);
    }

    void* caller = _ReturnAddress();
    const std::string caller_description = DescribeCallerAddress(caller);
    const std::string parameter_description = DescribePresentParameters(parameters);
    const std::string signature =
        "D3D9.CreateDevice|caller=" + caller_description + "|" +
        parameter_description + "|" + DescribeD3DResult(result);
    if (RememberD3DEvent(signature)) {
        Log(
            "IDirect3D9::CreateDevice caller=%s adapter=%u type=%lu hwnd=%s behavior=%s params=%s hr=%s device=%s",
            caller_description.c_str(),
            static_cast<unsigned int>(adapter),
            static_cast<unsigned long>(device_type),
            DescribeAddress(focus_window).c_str(),
            FormatHex32(behavior_flags).c_str(),
            parameter_description.c_str(),
            DescribeD3DResult(result).c_str(),
            device != nullptr ? DescribeAddress(*device).c_str() : "<null>");
    }
    MaybeLogD3DSummary();
    return result;
}

IDirect3D9* WINAPI HookedDirect3DCreate9(UINT sdk_version) {
    if (g_original_direct3d_create9 == nullptr) {
        return nullptr;
    }

    IDirect3D9* direct3d = g_original_direct3d_create9(sdk_version);
    if (!g_config.log_d3d9_traffic) {
        return direct3d;
    }

    InterlockedIncrement(&g_d3d_probe.direct3d_create9_calls);
    const std::string signature =
        "Direct3DCreate9|sdk=" + std::to_string(sdk_version) + "|" +
        DescribeAddress(direct3d);
    if (RememberD3DEvent(signature)) {
        Log(
            "Direct3DCreate9(sdk=0x%08lX) d3d=%s",
            static_cast<unsigned long>(sdk_version),
            DescribeAddress(direct3d).c_str());
    }
    MaybeLogD3DSummary();
    if (direct3d == nullptr) {
        return nullptr;
    }
    return new Direct3D9Proxy(direct3d);
}

HRESULT WINAPI HookedDirect3DCreate9Ex(UINT sdk_version, IDirect3D9Ex** direct3d) {
    if (g_original_direct3d_create9_ex == nullptr) {
        return E_FAIL;
    }

    const HRESULT result = g_original_direct3d_create9_ex(sdk_version, direct3d);
    if (!g_config.log_d3d9_traffic) {
        return result;
    }

    InterlockedIncrement(&g_d3d_probe.direct3d_create9_calls);
    const std::string signature =
        "Direct3DCreate9Ex|sdk=" + std::to_string(sdk_version) + "|" +
        DescribeD3DResult(result) + "|" +
        (direct3d != nullptr ? DescribeAddress(*direct3d) : std::string("<null>"));
    if (RememberD3DEvent(signature)) {
        Log(
            "Direct3DCreate9Ex(sdk=0x%08lX) hr=%s d3d=%s",
            static_cast<unsigned long>(sdk_version),
            DescribeD3DResult(result).c_str(),
            direct3d != nullptr ? DescribeAddress(*direct3d).c_str() : "<null>");
    }
    MaybeLogD3DSummary();

    if (SUCCEEDED(result) && direct3d != nullptr && *direct3d != nullptr) {
        *direct3d = new Direct3D9ExProxy(*direct3d);
    }
    return result;
}

FARPROC WINAPI HookedGetProcAddress(HMODULE module, LPCSTR proc_name) {
    if (g_original_get_proc_address == nullptr) {
        return nullptr;
    }

    const FARPROC result = g_original_get_proc_address(module, proc_name);
    if (!g_config.log_d3d9_traffic ||
        proc_name == nullptr ||
        IS_INTRESOURCE(proc_name) ||
        module == nullptr) {
        return result;
    }

    char module_path[MAX_PATH] = {};
    const DWORD module_path_len = GetModuleFileNameA(module, module_path, MAX_PATH);
    const char* module_name = module_path_len != 0 ? GetPathBaseName(module_path) : nullptr;
    if (module_name == nullptr || _stricmp(module_name, "d3d9.dll") != 0) {
        return result;
    }

    if (_stricmp(proc_name, "Direct3DCreate9Ex") == 0) {
        if (result != nullptr && g_original_direct3d_create9_ex == nullptr) {
            g_original_direct3d_create9_ex =
                reinterpret_cast<Direct3DCreate9ExFn>(result);
        }
        const std::string signature =
            std::string("GetProcAddress|d3d9.dll|") + proc_name;
        if (RememberD3DEvent(signature)) {
            Log("GetProcAddress(d3d9.dll, %s) redirected", proc_name);
        }
        return reinterpret_cast<FARPROC>(HookedDirect3DCreate9Ex);
    }

    if (_stricmp(proc_name, "Direct3DCreate9") == 0) {
        const std::string signature =
            std::string("GetProcAddress|d3d9.dll|") + proc_name;
        if (RememberD3DEvent(signature)) {
            Log("GetProcAddress(d3d9.dll, %s) redirected", proc_name);
        }
        return reinterpret_cast<FARPROC>(HookedDirect3DCreate9);
    }

    return result;
}

HRESULT WINAPI HookedDirectInput8Create(
    HINSTANCE instance,
    DWORD version,
    REFIID interface_id,
    LPVOID* output,
    LPUNKNOWN outer) {
    if (!g_original_direct_input8_create) {
        return E_FAIL;
    }

    const HRESULT result =
        g_original_direct_input8_create(instance, version, interface_id, output, outer);

    if (FAILED(result) || output == nullptr || *output == nullptr) {
        return result;
    }

    const bool should_wrap =
        g_config.log_input_traffic ||
        (g_supported_game_build && g_config.optimize_idle_get_cursor_pos);
    const std::string interface_name = DescribeGuid(interface_id);
    if (g_config.log_input_traffic &&
        RememberInputEvent("DirectInput8Create|" + interface_name)) {
        Log(
            "DirectInput8Create(version=0x%08lX, iid=%s) hr=0x%08lX",
            static_cast<unsigned long>(version),
            interface_name.c_str(),
            static_cast<unsigned long>(result));
    }

    if (!should_wrap) {
        return result;
    }

    if (IsEqualGuidValue(interface_id, kIID_DirectInput8A)) {
        *output = static_cast<IDirectInput8A*>(new DirectInput8AProxy(
            reinterpret_cast<IDirectInput8A*>(*output)));
    } else if (IsEqualGuidValue(interface_id, kIID_DirectInput8W)) {
        *output = static_cast<IDirectInput8W*>(new DirectInput8WProxy(
            reinterpret_cast<IDirectInput8W*>(*output)));
    }

    return result;
}

DWORD WINAPI HookedGetPrivateProfileStringA(
    LPCSTR section_name,
    LPCSTR key_name,
    LPCSTR default_value,
    LPSTR returned_string,
    DWORD size,
    LPCSTR file_name) {
    if (!g_original_get_private_profile_string_a) {
        return 0;
    }

    const DWORD result = g_original_get_private_profile_string_a(
        section_name, key_name, default_value, returned_string, size, file_name);

    if (!g_config.log_ini_traffic || !IsConfigIniPath(file_name)) {
        return result;
    }

    const std::string section = SanitizeAnsiForLog(section_name);
    const std::string key = SanitizeAnsiForLog(key_name);
    const std::string file = SanitizeAnsiForLog(file_name);
    const std::string signature = "READ|" + file + "|" + section + "|" + key;

    if (RememberIniEvent(g_seen_ini_reads, signature)) {
        Log(
            "INI read section=%s key=%s result_len=%lu value=%s file=%s",
            section.c_str(),
            key.c_str(),
            static_cast<unsigned long>(result),
            SanitizeAnsiForLog(returned_string).c_str(),
            file.c_str());
    }

    return result;
}

BOOL WINAPI HookedWritePrivateProfileStringA(
    LPCSTR section_name,
    LPCSTR key_name,
    LPCSTR value,
    LPCSTR file_name) {
    if (!g_original_write_private_profile_string_a) {
        return FALSE;
    }

    const BOOL result =
        g_original_write_private_profile_string_a(section_name, key_name, value, file_name);

    if (!g_config.log_ini_traffic || !IsConfigIniPath(file_name)) {
        return result;
    }

    const std::string section = SanitizeAnsiForLog(section_name);
    const std::string key = SanitizeAnsiForLog(key_name);
    const std::string sanitized_value = SanitizeAnsiForLog(value);
    const std::string file = SanitizeAnsiForLog(file_name);
    const std::string signature =
        "WRITE|" + file + "|" + section + "|" + key + "|" + sanitized_value;

    if (RememberIniEvent(g_seen_ini_writes, signature)) {
        Log(
            "INI write section=%s key=%s value=%s success=%d file=%s",
            section.c_str(),
            key.c_str(),
            sanitized_value.c_str(),
            result ? 1 : 0,
            file.c_str());
    }

    return result;
}

bool WriteCrashDump(PEXCEPTION_POINTERS exception_pointers) {
    if (!g_config.crash_dumps || exception_pointers == nullptr) {
        return false;
    }

    if (InterlockedCompareExchange(&g_dump_written, 1, 0) != 0) {
        return false;
    }

    EnsureDirectory(g_data_dir);
    EnsureDirectory(g_dump_dir);

    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);

    wchar_t file_name[MAX_PATH] = {};
    std::swprintf(
        file_name,
        MAX_PATH,
        L"%s\\BioPatch_%04u%02u%02u_%02u%02u%02u_%lu.dmp",
        g_dump_dir.c_str(),
        local_time.wYear,
        local_time.wMonth,
        local_time.wDay,
        local_time.wHour,
        local_time.wMinute,
        local_time.wSecond,
        GetCurrentProcessId());

    HANDLE file = CreateFileW(
        file_name,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Log("Failed to create crash dump file.");
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception_pointers;
    info.ClientPointers = FALSE;

    const BOOL result = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        static_cast<MINIDUMP_TYPE>(
            MiniDumpWithDataSegs |
            MiniDumpWithHandleData |
            MiniDumpWithThreadInfo |
            MiniDumpWithIndirectlyReferencedMemory),
        &info,
        nullptr,
        nullptr);

    CloseHandle(file);

    if (result) {
        Log("Wrote crash dump to %ls", file_name);
        return true;
    }

    Log("MiniDumpWriteDump failed with %lu", GetLastError());
    return false;
}

LONG WINAPI TopLevelExceptionFilter(PEXCEPTION_POINTERS exception_pointers) {
    WriteCrashDump(exception_pointers);

    if (g_downstream_exception_filter != nullptr &&
        g_downstream_exception_filter != TopLevelExceptionFilter) {
        return g_downstream_exception_filter(exception_pointers);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI HookedSetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    const auto previous =
        g_downstream_exception_filter != nullptr ? g_downstream_exception_filter
                                                 : TopLevelExceptionFilter;
    g_downstream_exception_filter = filter;

    if (g_original_set_unhandled_exception_filter) {
        g_original_set_unhandled_exception_filter(TopLevelExceptionFilter);
    }

    Log("Captured SetUnhandledExceptionFilter(%p)", filter);
    return previous;
}

void InitializePaths() {
    g_module_path = GetModulePath(g_module);
    g_module_dir = GetDirectoryName(g_module_path);
    g_data_dir = g_module_dir + L"\\BioPatch";
    g_dump_dir = g_data_dir + L"\\crashdumps";
    g_ini_path = g_module_dir + L"\\BioPatch.ini";
    g_log_path = g_data_dir + L"\\BioPatch.log";

    EnsureDirectory(g_data_dir);
    EnsureDirectory(g_dump_dir);
}

void ResolveOriginalFunctions() {
    HMODULE winmm = GetModuleHandleW(L"winmm.dll");
    if (!winmm) {
        winmm = LoadLibraryW(L"winmm.dll");
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE kernel_base = GetModuleHandleW(L"KernelBase.dll");
    if (!kernel_base) {
        kernel_base = LoadLibraryW(L"KernelBase.dll");
    }

    if (winmm && !g_original_time_get_time) {
        g_original_time_get_time =
            reinterpret_cast<TimeGetTimeFn>(GetProcAddress(winmm, "timeGetTime"));
    }

    if (winmm && !g_original_time_begin_period) {
        g_original_time_begin_period =
            reinterpret_cast<TimeBeginPeriodFn>(GetProcAddress(winmm, "timeBeginPeriod"));
    }

    if (winmm && !g_original_time_end_period) {
        g_original_time_end_period =
            reinterpret_cast<TimeEndPeriodFn>(GetProcAddress(winmm, "timeEndPeriod"));
    }

    if (kernel32 && !g_original_sleep) {
        g_original_sleep = reinterpret_cast<SleepFn>(GetProcAddress(kernel32, "Sleep"));
    }

    if (kernel32 && !g_original_sleep_ex) {
        g_original_sleep_ex = reinterpret_cast<SleepExFn>(GetProcAddress(kernel32, "SleepEx"));
    }

    if (kernel32 && !g_original_get_exit_code_thread) {
        g_original_get_exit_code_thread = reinterpret_cast<GetExitCodeThreadFn>(
            GetProcAddress(kernel32, "GetExitCodeThread"));
    }

    if (kernel32 && !g_original_get_thread_id) {
        g_original_get_thread_id =
            reinterpret_cast<GetThreadIdFn>(GetProcAddress(kernel32, "GetThreadId"));
    }

    if (kernel32 && !g_original_get_proc_address) {
        g_original_get_proc_address =
            reinterpret_cast<GetProcAddressFn>(GetProcAddress(kernel32, "GetProcAddress"));
    }

    if (kernel32 && !g_original_wait_for_single_object) {
        g_original_wait_for_single_object = reinterpret_cast<WaitForSingleObjectFn>(
            GetProcAddress(kernel32, "WaitForSingleObject"));
    }

    if (kernel32 && !g_original_wait_for_multiple_objects) {
        g_original_wait_for_multiple_objects = reinterpret_cast<WaitForMultipleObjectsFn>(
            GetProcAddress(kernel32, "WaitForMultipleObjects"));
    }

    if (g_wait_on_address == nullptr) {
        if (kernel32) {
            g_wait_on_address =
                reinterpret_cast<WaitOnAddressFn>(GetProcAddress(kernel32, "WaitOnAddress"));
        }
        if (g_wait_on_address == nullptr && kernel_base) {
            g_wait_on_address =
                reinterpret_cast<WaitOnAddressFn>(GetProcAddress(kernel_base, "WaitOnAddress"));
        }
    }

    if (g_wake_by_address_all == nullptr) {
        if (kernel32) {
            g_wake_by_address_all = reinterpret_cast<WakeByAddressAllFn>(
                GetProcAddress(kernel32, "WakeByAddressAll"));
        }
        if (g_wake_by_address_all == nullptr && kernel_base) {
            g_wake_by_address_all = reinterpret_cast<WakeByAddressAllFn>(
                GetProcAddress(kernel_base, "WakeByAddressAll"));
        }
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        user32 = LoadLibraryW(L"user32.dll");
    }

    if (user32 && !g_original_get_cursor_pos) {
        g_original_get_cursor_pos =
            reinterpret_cast<GetCursorPosFn>(GetProcAddress(user32, "GetCursorPos"));
    }

    if (user32 && !g_original_msg_wait_for_multiple_objects_ex) {
        g_original_msg_wait_for_multiple_objects_ex =
            reinterpret_cast<MsgWaitForMultipleObjectsExFn>(
                GetProcAddress(user32, "MsgWaitForMultipleObjectsEx"));
    }

    if (user32 && !g_original_client_to_screen) {
        g_original_client_to_screen =
            reinterpret_cast<ClientToScreenFn>(GetProcAddress(user32, "ClientToScreen"));
    }

    if (user32 && !g_original_get_client_rect) {
        g_original_get_client_rect =
            reinterpret_cast<GetClientRectFn>(GetProcAddress(user32, "GetClientRect"));
    }

    if (user32 && !g_original_clip_cursor) {
        g_original_clip_cursor =
            reinterpret_cast<ClipCursorFn>(GetProcAddress(user32, "ClipCursor"));
    }

    HMODULE dinput8 = GetModuleHandleW(L"dinput8.dll");
    if (!dinput8) {
        dinput8 = LoadLibraryW(L"dinput8.dll");
    }

    if (dinput8 && !g_original_direct_input8_create) {
        g_original_direct_input8_create = reinterpret_cast<DirectInput8CreateFn>(
            GetProcAddress(dinput8, "DirectInput8Create"));
    }

    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    if (!d3d9) {
        d3d9 = LoadLibraryW(L"d3d9.dll");
    }

    if (d3d9 && !g_original_direct3d_create9) {
        g_original_direct3d_create9 =
            reinterpret_cast<Direct3DCreate9Fn>(GetProcAddress(d3d9, "Direct3DCreate9"));
    }

    if (d3d9 && !g_original_direct3d_create9_ex) {
        g_original_direct3d_create9_ex =
            reinterpret_cast<Direct3DCreate9ExFn>(GetProcAddress(d3d9, "Direct3DCreate9Ex"));
    }

    if (kernel32 && !g_original_get_private_profile_string_a) {
        g_original_get_private_profile_string_a =
            reinterpret_cast<GetPrivateProfileStringAFn>(
                GetProcAddress(kernel32, "GetPrivateProfileStringA"));
    }

    if (kernel32 && !g_original_write_private_profile_string_a) {
        g_original_write_private_profile_string_a =
            reinterpret_cast<WritePrivateProfileStringAFn>(
                GetProcAddress(kernel32, "WritePrivateProfileStringA"));
    }

    if (kernel32 && !g_original_set_unhandled_exception_filter) {
        g_original_set_unhandled_exception_filter =
            reinterpret_cast<SetUnhandledExceptionFilterFn>(
                GetProcAddress(kernel32, "SetUnhandledExceptionFilter"));
    }
}

void InitializeTimerState() {
    if (!g_original_time_get_time) {
        return;
    }

    QueryPerformanceFrequency(&g_timer.frequency);
    QueryPerformanceCounter(&g_timer.start_qpc);
    g_timer.base_time_get_time = g_original_time_get_time();
    g_timer.ready = g_timer.frequency.QuadPart != 0;

    if (g_timer.ready) {
        Log(
            "Timer shim ready. base=%lu freq=%lld",
            g_timer.base_time_get_time,
            g_timer.frequency.QuadPart);
    } else {
        Log("Timer shim disabled because QPC frequency was zero.");
    }
}

void InstallHooks() {
    HMODULE main_module = GetModuleHandleW(nullptr);
    if (!main_module) {
        Log("Failed to resolve the main module.");
        return;
    }

    ResolveOriginalFunctions();
    InitializeTimerState();
    g_supported_game_build = IsSupportedGameBuild(main_module);
    if (g_supported_game_build) {
        Log("Validated supported Resident Evil Revelations 2 executable build.");
    }

    if (g_config.optimize_worker_state_wait) {
        if (!g_supported_game_build) {
            Log("Worker-state wait optimization disabled: unsupported game executable build.");
        } else if (g_wait_on_address == nullptr || g_wake_by_address_all == nullptr) {
            Log("Worker-state wait optimization unavailable: WaitOnAddress/WakeByAddressAll missing.");
        } else {
            auto* base = reinterpret_cast<std::uint8_t*>(main_module);

            void* original_request3 = g_original_worker_state_request3;
            if (InstallInlineHook(
                    base + kWorkerStateRequest3Rva,
                    reinterpret_cast<void*>(HookedWorkerStateRequest3),
                    &original_request3,
                    kWorkerStateRequestHookSize)) {
                g_original_worker_state_request3 = original_request3;
                Log("Installed worker-state request3 hook.");
            } else {
                Log("Failed to install worker-state request3 hook.");
            }

            void* original_request4 = g_original_worker_state_request4;
            if (InstallInlineHook(
                    base + kWorkerStateRequest4Rva,
                    reinterpret_cast<void*>(HookedWorkerStateRequest4),
                    &original_request4,
                    kWorkerStateRequestHookSize)) {
                g_original_worker_state_request4 = original_request4;
                Log("Installed worker-state request4 hook.");
            } else {
                Log("Failed to install worker-state request4 hook.");
            }

            void* original_request5 = g_original_worker_state_request5;
            if (InstallInlineHook(
                    base + kWorkerStateRequest5Rva,
                    reinterpret_cast<void*>(HookedWorkerStateRequest5),
                    &original_request5,
                    kWorkerStateRequestHookSize)) {
                g_original_worker_state_request5 = original_request5;
                Log("Installed worker-state request5 hook.");
            } else {
                Log("Failed to install worker-state request5 hook.");
            }

            void* original_dispatch = g_original_worker_state_dispatch;
            if (InstallInlineHook(
                    base + kWorkerStateDispatchRva,
                    reinterpret_cast<void*>(HookedWorkerStateDispatch),
                    &original_dispatch,
                    kWorkerStateDispatchHookSize)) {
                g_original_worker_state_dispatch = original_dispatch;
                Log("Installed worker-state dispatch hook.");
            } else {
                Log("Failed to install worker-state dispatch hook.");
            }
        }
    }

    if (g_config.optimize_mt_worker_wait) {
        if (!g_supported_game_build) {
            Log("MT worker wait optimization disabled: unsupported game executable build.");
        } else if (g_wait_on_address == nullptr || g_wake_by_address_all == nullptr) {
            Log("MT worker wait optimization unavailable: WaitOnAddress/WakeByAddressAll missing.");
        } else {
            auto* base = reinterpret_cast<std::uint8_t*>(main_module);

            void* original_request_state4 = g_original_mt_worker_request_state4;
            if (InstallInlineHook(
                    base + kMtWorkerRequestState4Rva,
                    reinterpret_cast<void*>(HookedMtWorkerRequestState4),
                    &original_request_state4,
                    kMtWorkerRequestState4HookSize)) {
                g_original_mt_worker_request_state4 = original_request_state4;
                Log("Installed MT worker state4 hook.");
            } else {
                Log("Failed to install MT worker state4 hook.");
            }

            void* original_request_state5 = g_original_mt_worker_request_state5;
            if (InstallInlineHook(
                    base + kMtWorkerRequestState5Rva,
                    reinterpret_cast<void*>(HookedMtWorkerRequestState5),
                    &original_request_state5,
                    kMtWorkerRequestState5HookSize)) {
                g_original_mt_worker_request_state5 = original_request_state5;
                Log("Installed MT worker state5 hook.");
            } else {
                Log("Failed to install MT worker state5 hook.");
            }

            void* original_request_state8 = g_original_mt_worker_request_state8;
            if (InstallInlineHook(
                    base + kMtWorkerRequestState8Rva,
                    reinterpret_cast<void*>(HookedMtWorkerRequestState8),
                    &original_request_state8,
                    kMtWorkerRequestState8HookSize)) {
                g_original_mt_worker_request_state8 = original_request_state8;
                Log("Installed MT worker state8 hook.");
            } else {
                Log("Failed to install MT worker state8 hook.");
            }
        }
    }

    if (g_config.high_precision_time_get_time) {
        void* original = reinterpret_cast<void*>(g_original_time_get_time);
        if (PatchIAT(main_module, "WINMM.dll", "timeGetTime", HookedTimeGetTime, &original)) {
            g_original_time_get_time = reinterpret_cast<TimeGetTimeFn>(original);
            Log("Installed timeGetTime hook.");
        } else {
            Log("Failed to install timeGetTime hook.");
        }
    }

    const bool needs_sleep_hook =
        g_config.log_timing_traffic ||
        g_config.precise_short_sleep ||
        (g_supported_game_build &&
            (g_config.optimize_ui_thread_message_wait ||
             g_config.optimize_queue_worker_yield ||
             g_config.optimize_pacing_sleep_precision ||
             g_config.optimize_legacy_delay_precision ||
             g_config.optimize_stream_retry_backoff ||
             g_config.optimize_stream_retry_yield ||
             g_config.optimize_thread_join_wait ||
             g_config.optimize_mt_worker_wait ||
             g_config.optimize_mt_worker_state4_timer_wait ||
             g_config.optimize_mt_worker_state9_wait));
    if (needs_sleep_hook) {
        void* original_sleep = reinterpret_cast<void*>(g_original_sleep);
        if (PatchIAT(main_module, "KERNEL32.dll", "Sleep", HookedSleep, &original_sleep)) {
            g_original_sleep = reinterpret_cast<SleepFn>(original_sleep);
            Log("Installed Sleep hook.");
        } else {
            Log("Failed to install Sleep hook.");
        }
    }

    if (g_config.log_timing_traffic || g_config.precise_short_sleep) {
        void* original_sleep_ex = reinterpret_cast<void*>(g_original_sleep_ex);
        if (PatchIAT(main_module, "KERNEL32.dll", "SleepEx", HookedSleepEx, &original_sleep_ex)) {
            g_original_sleep_ex = reinterpret_cast<SleepExFn>(original_sleep_ex);
            Log("Installed SleepEx hook.");
        } else {
            Log("Failed to install SleepEx hook.");
        }
    }

    if (g_config.optimize_thread_join_wait) {
        g_thread_join_hook_installed = false;
        if (!g_supported_game_build) {
            Log("Thread-join wait optimization disabled: unsupported game executable build.");
        } else if (g_original_get_exit_code_thread == nullptr ||
            g_original_wait_for_single_object == nullptr ||
            g_original_get_thread_id == nullptr) {
            Log("Thread-join wait optimization unavailable: missing kernel32 exports.");
        } else {
            void* original_get_exit_code_thread =
                reinterpret_cast<void*>(g_original_get_exit_code_thread);
            if (PatchIAT(
                    main_module,
                    "KERNEL32.dll",
                    "GetExitCodeThread",
                    HookedGetExitCodeThread,
                    &original_get_exit_code_thread)) {
                g_original_get_exit_code_thread =
                    reinterpret_cast<GetExitCodeThreadFn>(original_get_exit_code_thread);
                g_thread_join_hook_installed = true;
                Log("Installed GetExitCodeThread hook.");
            } else {
                Log("Failed to install GetExitCodeThread hook.");
            }
        }
    }

    const bool needs_timer_period_hook =
        g_config.log_timing_traffic ||
        (g_supported_game_build &&
            g_config.optimize_worker_state_wait &&
            g_config.optimize_ui_thread_timer_period);
    if (needs_timer_period_hook) {
        void* original_time_begin_period =
            reinterpret_cast<void*>(g_original_time_begin_period);
        void* original_time_end_period = reinterpret_cast<void*>(g_original_time_end_period);

        if (PatchIAT(
                main_module,
                "WINMM.dll",
                "timeBeginPeriod",
                HookedTimeBeginPeriod,
                &original_time_begin_period)) {
            g_original_time_begin_period =
                reinterpret_cast<TimeBeginPeriodFn>(original_time_begin_period);
            Log("Installed timeBeginPeriod hook.");
        } else {
            Log("Failed to install timeBeginPeriod hook.");
        }

        if (PatchIAT(
                main_module,
                "WINMM.dll",
                "timeEndPeriod",
                HookedTimeEndPeriod,
                &original_time_end_period)) {
            g_original_time_end_period =
                reinterpret_cast<TimeEndPeriodFn>(original_time_end_period);
            Log("Installed timeEndPeriod hook.");
        } else {
            Log("Failed to install timeEndPeriod hook.");
        }
    }

    if (g_config.log_timing_traffic) {
        void* original_wait_for_single_object =
            reinterpret_cast<void*>(g_original_wait_for_single_object);
        void* original_wait_for_multiple_objects =
            reinterpret_cast<void*>(g_original_wait_for_multiple_objects);

        if (PatchIAT(
                main_module,
                "KERNEL32.dll",
                "WaitForSingleObject",
                HookedWaitForSingleObject,
                &original_wait_for_single_object)) {
            g_original_wait_for_single_object =
                reinterpret_cast<WaitForSingleObjectFn>(original_wait_for_single_object);
            Log("Installed WaitForSingleObject hook.");
        } else {
            Log("Failed to install WaitForSingleObject hook.");
        }

        if (PatchIAT(
                main_module,
                "KERNEL32.dll",
                "WaitForMultipleObjects",
                HookedWaitForMultipleObjects,
                &original_wait_for_multiple_objects)) {
            g_original_wait_for_multiple_objects =
                reinterpret_cast<WaitForMultipleObjectsFn>(original_wait_for_multiple_objects);
            Log("Installed WaitForMultipleObjects hook.");
        } else {
            Log("Failed to install WaitForMultipleObjects hook.");
        }
    }

    if (g_config.crash_dumps && g_original_set_unhandled_exception_filter) {
        g_original_set_unhandled_exception_filter(TopLevelExceptionFilter);
        Log("Installed top-level crash handler.");
    }

    if (g_config.log_ini_traffic) {
        void* original_get_private_profile_string_a =
            reinterpret_cast<void*>(g_original_get_private_profile_string_a);
        void* original_write_private_profile_string_a =
            reinterpret_cast<void*>(g_original_write_private_profile_string_a);

        if (PatchIAT(
                main_module,
                "KERNEL32.dll",
                "GetPrivateProfileStringA",
                HookedGetPrivateProfileStringA,
                &original_get_private_profile_string_a)) {
            g_original_get_private_profile_string_a =
                reinterpret_cast<GetPrivateProfileStringAFn>(
                    original_get_private_profile_string_a);
            Log("Installed GetPrivateProfileStringA hook.");
        } else {
            Log("Failed to install GetPrivateProfileStringA hook.");
        }

        if (PatchIAT(
                main_module,
                "KERNEL32.dll",
                "WritePrivateProfileStringA",
                HookedWritePrivateProfileStringA,
                &original_write_private_profile_string_a)) {
            g_original_write_private_profile_string_a =
                reinterpret_cast<WritePrivateProfileStringAFn>(
                    original_write_private_profile_string_a);
            Log("Installed WritePrivateProfileStringA hook.");
        } else {
            Log("Failed to install WritePrivateProfileStringA hook.");
        }
    }

    if (g_config.log_d3d9_traffic) {
        void* original_get_proc_address = reinterpret_cast<void*>(g_original_get_proc_address);
        void* original_direct3d_create9 = reinterpret_cast<void*>(g_original_direct3d_create9);
        if (PatchIAT(
                main_module,
                "KERNEL32.dll",
                "GetProcAddress",
                HookedGetProcAddress,
                &original_get_proc_address)) {
            g_original_get_proc_address =
                reinterpret_cast<GetProcAddressFn>(original_get_proc_address);
            Log("Installed GetProcAddress hook.");
        } else {
            Log("Failed to install GetProcAddress hook.");
        }

        if (PatchIAT(
                main_module,
                "d3d9.dll",
                "Direct3DCreate9",
                HookedDirect3DCreate9,
                &original_direct3d_create9)) {
            g_original_direct3d_create9 =
                reinterpret_cast<Direct3DCreate9Fn>(original_direct3d_create9);
            Log("Installed Direct3DCreate9 hook.");
        } else {
            Log("Failed to install Direct3DCreate9 hook.");
        }
    }

    if (g_config.log_input_traffic) {
        if (!g_supported_game_build) {
            Log("QFPS element copy probe disabled: unsupported game executable build.");
        } else {
            auto* base = reinterpret_cast<std::uint8_t*>(main_module);
            void* original_qfps_element_copy = g_original_qfps_element_copy;
            if (InstallInlineHook(
                    base + kQfpsElementCopyRva,
                    reinterpret_cast<void*>(HookedQfpsElementCopy),
                    &original_qfps_element_copy,
                    kQfpsElementCopyHookSize)) {
                g_original_qfps_element_copy = original_qfps_element_copy;
                Log("Installed QFPS element copy hook.");
            } else {
                Log("Failed to install QFPS element copy hook.");
            }
        }
    }

    if (g_config.log_input_traffic ||
        (g_supported_game_build &&
            (g_config.optimize_get_cursor_pos_pair ||
             g_config.optimize_idle_get_cursor_pos ||
             g_config.disable_qfps_mouse_ctrl_lag))) {
        void* original_get_cursor_pos = reinterpret_cast<void*>(g_original_get_cursor_pos);
        if (PatchIAT(
                main_module,
                "USER32.dll",
                "GetCursorPos",
                HookedGetCursorPos,
                &original_get_cursor_pos)) {
            g_original_get_cursor_pos =
                reinterpret_cast<GetCursorPosFn>(original_get_cursor_pos);
            Log("Installed GetCursorPos hook.");
        } else {
            Log("Failed to install GetCursorPos hook.");
        }
    }

    if (g_config.log_input_traffic ||
        (g_supported_game_build && g_config.optimize_screen_to_client_pair)) {
        void* original_screen_to_client = reinterpret_cast<void*>(g_original_screen_to_client);
        if (PatchIAT(
                main_module,
                "USER32.dll",
                "ScreenToClient",
                HookedScreenToClient,
                &original_screen_to_client)) {
            g_original_screen_to_client =
                reinterpret_cast<ScreenToClientFn>(original_screen_to_client);
            Log("Installed ScreenToClient hook.");
        } else {
            Log("Failed to install ScreenToClient hook.");
        }
    }

    if (g_config.log_input_traffic ||
        (g_supported_game_build && g_config.optimize_client_to_screen_origin)) {
        void* original_client_to_screen = reinterpret_cast<void*>(g_original_client_to_screen);
        if (PatchIAT(
                main_module,
                "USER32.dll",
                "ClientToScreen",
                HookedClientToScreen,
                &original_client_to_screen)) {
            g_original_client_to_screen =
                reinterpret_cast<ClientToScreenFn>(original_client_to_screen);
            Log("Installed ClientToScreen hook.");
        } else {
            Log("Failed to install ClientToScreen hook.");
        }
    }

    if (g_config.log_input_traffic ||
        (g_supported_game_build && g_config.optimize_get_client_rect_pair)) {
        void* original_get_client_rect = reinterpret_cast<void*>(g_original_get_client_rect);
        if (PatchIAT(
                main_module,
                "USER32.dll",
                "GetClientRect",
                HookedGetClientRect,
                &original_get_client_rect)) {
            g_original_get_client_rect =
                reinterpret_cast<GetClientRectFn>(original_get_client_rect);
            Log("Installed GetClientRect hook.");
        } else {
            Log("Failed to install GetClientRect hook.");
        }
    }

    if (g_config.log_input_traffic ||
        (g_supported_game_build && g_config.optimize_idle_get_cursor_pos)) {
        void* original_direct_input8_create =
            reinterpret_cast<void*>(g_original_direct_input8_create);
        if (PatchIAT(
                main_module,
                "DINPUT8.dll",
                "DirectInput8Create",
                HookedDirectInput8Create,
                &original_direct_input8_create)) {
            g_original_direct_input8_create =
                reinterpret_cast<DirectInput8CreateFn>(original_direct_input8_create);
            Log("Installed DirectInput8Create hook.");
        } else {
            Log("Failed to install DirectInput8Create hook.");
        }
    }

    if (g_config.log_input_traffic || g_config.optimize_clip_cursor) {
        void* original_clip_cursor = reinterpret_cast<void*>(g_original_clip_cursor);
        if (PatchIAT(
                main_module,
                "USER32.dll",
                "ClipCursor",
                HookedClipCursor,
                &original_clip_cursor)) {
            g_original_clip_cursor = reinterpret_cast<ClipCursorFn>(original_clip_cursor);
            Log("Installed ClipCursor hook.");
        } else {
            Log("Failed to install ClipCursor hook.");
        }
    }

    if (g_config.protect_unhandled_exception_filter &&
        g_original_set_unhandled_exception_filter) {
        void* original = reinterpret_cast<void*>(g_original_set_unhandled_exception_filter);
        if (PatchIAT(
                main_module,
                "KERNEL32.dll",
                "SetUnhandledExceptionFilter",
                HookedSetUnhandledExceptionFilter,
                &original)) {
            g_original_set_unhandled_exception_filter =
                reinterpret_cast<SetUnhandledExceptionFilterFn>(original);
            Log("Installed SetUnhandledExceptionFilter guard.");
        } else {
            Log("Failed to install SetUnhandledExceptionFilter guard.");
        }
    }

}

DWORD WINAPI InitializeThread(void*) {
    InitializeCriticalSection(&g_log_lock);
    g_log_lock_ready = true;
    InitializeCriticalSection(&g_ini_probe_lock);
    g_ini_probe_lock_ready = true;

    InitializePaths();
    LoadConfig();

    Log("Initializing BioPatch");
    Log("Module path: %ls", g_module_path.c_str());
    Log("INI path: %ls", g_ini_path.c_str());

    InstallHooks();
    Log("BioPatch initialization complete.");
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, InitializeThread, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_ini_probe_lock_ready) {
            DeleteCriticalSection(&g_ini_probe_lock);
            g_ini_probe_lock_ready = false;
        }
        if (g_log_lock_ready) {
            DeleteCriticalSection(&g_log_lock);
            g_log_lock_ready = false;
        }
    }

    return TRUE;
}
