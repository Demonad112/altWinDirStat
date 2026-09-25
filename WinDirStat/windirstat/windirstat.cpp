// windirstat.cpp	- Implementation of CDirstatApp and some globals
//
// see `file_header_text.txt` for licensing & contact info. If you can't find that file, then assume you're NOT allowed to do whatever you wanted to do.
#include "stdafx.h"

#pragma once


#ifndef WDS_WINDIRSTAT_CPP
#define WDS_WINDIRSTAT_CPP

WDS_FILE_INCLUDE_MESSAGE


#include "macros_that_scare_small_children.h"
#include "graphview.h"
#include "selectdrivesdlg.h"
#include "TreeListControl.h"	// CTreeListItem::GetPath
#include "dirstatdoc.h"
#include "options.h"
#include "windirstat.h"
#include "mainframe.h"
#include "globalhelpers.h"
#include "ScopeGuard.h"
#include "COM_helpers.h"

#include "stringformatting.h"


CMainFrame* GetMainFrame( ) {
	// Not: `return (CMainFrame *)AfxGetMainWnd();` because CWinApp::m_pMainWnd is set too late.
	return CMainFrame::GetTheFrame( );
	}

CDirstatApp* GetApp( ) {
	return static_cast< CDirstatApp* >( AfxGetApp( ) );
	}


namespace {

#ifdef DEBUG
	void setFlags( ) {
		const auto flag = ::_CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
		TRACE( _T( "CrtDbg state: %i\r\n\t_CRTDBG_ALLOC_MEM_DF: %i\r\n\t_CRTDBG_CHECK_CRT_DF: %i\r\n\t_CRTDBG_LEAK_CHECK_DF: %i\r\n\t_CRTDBG_DELAY_FREE_MEM_DEF: %i\r\n" ), flag, ( flag & _CRTDBG_ALLOC_MEM_DF ), ( flag & _CRTDBG_CHECK_CRT_DF ), ( flag & _CRTDBG_LEAK_CHECK_DF ), ( flag & _CRTDBG_DELAY_FREE_MEM_DF ) );
		
		::_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF bitor _CRTDBG_LEAK_CHECK_DF );
		const auto flag2 = ::_CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
		TRACE( _T( "CrtDbg state: %i\r\n\t_CRTDBG_ALLOC_MEM_DF: %i\r\n\t_CRTDBG_CHECK_CRT_DF: %i\r\n\t_CRTDBG_LEAK_CHECK_DF: %i\r\n\t_CRTDBG_DELAY_FREE_MEM_DEF: %i\r\n" ), flag2, ( flag2 & _CRTDBG_ALLOC_MEM_DF ), ( flag2 & _CRTDBG_CHECK_CRT_DF ), ( flag2 & _CRTDBG_LEAK_CHECK_DF ), ( flag2 & _CRTDBG_DELAY_FREE_MEM_DF )  );
		}
#endif

	/*
WINBASEAPI
BOOL
WINAPI
SetProcessMitigationPolicy(
    _In_ PROCESS_MITIGATION_POLICY MitigationPolicy,
    _In_reads_bytes_(dwLength) PVOID lpBuffer,
    _In_ SIZE_T dwLength
    );

	*/


	typedef WINBASEAPI BOOL( WINAPI* SetProcessMitigationPolicy_t )( _In_ _Const_ PROCESS_MITIGATION_POLICY MitigationPolicy, _In_reads_bytes_( dwLength ) _Const_ PVOID lpBuffer, _In_ _Const_ SIZE_T dwLength );


	struct HMODULE_RAII final {
		HMODULE_RAII( _In_ std::pair<HMODULE, BOOL> pair_in ) : the_module { pair_in.first }, result { pair_in.second } { }
		~HMODULE_RAII( ) {
			if ( result == 0 ) {
				return;
				}
			const BOOL free_result = ::FreeLibrary( the_module );
			ASSERT( free_result != 0 );
			if ( free_result != 0 ) {
				return;
				}
			displayWindowsMsgBoxWithMessage( L"FreeLibrary( the_module ) failed!" );
			displayWindowsMsgBoxWithError( );
			}
		DISALLOW_COPY_AND_ASSIGN( HMODULE_RAII );
		const HMODULE the_module;
		const BOOL    result;
		};

	void handle_mitigation_failure_doublefault( _In_z_ PCWSTR const mitigation_specific_error_message, _In_z_ PCWSTR const non_mitigation_specific_message ) {
		displayWindowsMsgBoxWithMessage( non_mitigation_specific_message );
		displayWindowsMsgBoxWithMessage( mitigation_specific_error_message );
		}

	//CStyle_GetLastErrorAsFormattedMessage appends a newline (`\r\n`). Clobber that new fucking line:
	void clobber_the_damned_new_line( _Inout_z_ PWSTR str_err_buff, _In_ const rsize_t chars_written ) {
		if ( chars_written > 1 ) {
			str_err_buff[ chars_written - 1 ] = 0;
			str_err_buff[ chars_written - 2 ] = 0;
			}
		}

	std::wstring get_last_error_no_newline( _In_z_ PCWSTR const mitigation_specific_error_message ) {
		constexpr const rsize_t str_buff_size = 256u;
		wchar_t str_err_buff[ str_buff_size ] = { 0 };
		rsize_t chars_written_1 = 0u;
		const HRESULT err_fmt_res = CStyle_GetLastErrorAsFormattedMessage( str_err_buff, str_buff_size, chars_written_1 );
		ASSERT( SUCCEEDED( err_fmt_res ) );
		if ( !SUCCEEDED( err_fmt_res ) ) {
			handle_mitigation_failure_doublefault( mitigation_specific_error_message, L"Ran into an error while formatting another error! This happened in the handler for enhanced-security mitigation initializations!" );
			return std::wstring( L"" );
			}
		clobber_the_damned_new_line( str_err_buff, chars_written_1 );
		return std::wstring( str_err_buff );
		}

	//TODO: BUGBUG: refactor when not half-asleep
	void handle_mitigation_enable_failure( _In_z_ PCWSTR const mitigation_specific_error_message ) {
		const std::wstring dyn_str( mitigation_specific_error_message + get_last_error_no_newline( mitigation_specific_error_message ) + L" It's totally safe to continue execution (we will), but if you see this, please report it to me." );
		//dyn_str += L" It's totally safe to continue execution (we will), but if you see this, please report it to me.";
		displayWindowsMsgBoxWithMessage( dyn_str.c_str( ) );
		}


	//Tell windows that we WANT to crash if the shit hits the fan. This is a security feature.
	void enable_heap_security_crash_on_corruption( ) {

		//If the function succeeds, the return value is nonzero.
		const BOOL heap_set_info_result = ::HeapSetInformation( NULL, HeapEnableTerminationOnCorruption, NULL, 0u );
		if ( heap_set_info_result == 0 ) {
			TRACE( _T( "HeapSetInformation failed!\r\n" ) );
			}
		else {
			TRACE( _T( "Enabled HeapEnableTerminationOnCorruption!\r\n" ) );
			}
		}


	void enable_ASLR_mitigation( SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f ) {
		PROCESS_MITIGATION_ASLR_POLICY ASLR_policy = { 0 };
		ASLR_policy.EnableBottomUpRandomization = true;
		ASLR_policy.EnableForceRelocateImages   = true;
#ifdef _WIN64
		ASLR_policy.EnableHighEntropy           = true;
#endif
		ASLR_policy.DisallowStrippedImages = true;
		
		auto guard = WDS_SCOPEGUARD_INSTANCE( [&]{ handle_mitigation_enable_failure( L"Failed to set enhanced/forced ASLR: " ); } );

		const BOOL set_aslr_policy_res = SetProcessMitigationPolicy_f( ProcessASLRPolicy, &ASLR_policy, sizeof( ASLR_policy ) );
		if ( set_aslr_policy_res == TRUE ) {
			TRACE( _T( "Successfully enabled bottom-up randomization, forcible image relocation, and refusal to load images without a `.reloc` section (DisallowStrippedImages).\r\n" ) );
			guard.dismiss( );
			return;
			}
		}

	//This one seems to be causing trouble. Disable for now.
	void enable_DEP_mitigation( SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f ) {
		PROCESS_MITIGATION_DEP_POLICY DEP_policy = { 0 };
		DEP_policy.Enable                   = true;
		DEP_policy.Permanent                = true;
		DEP_policy.DisableAtlThunkEmulation = true;

		auto guard = WDS_SCOPEGUARD_INSTANCE( [ &] { handle_mitigation_enable_failure( L"Failed to set enhanced/forced DEP: " ); } );

		const BOOL set_DEP_policy_res = SetProcessMitigationPolicy_f( ProcessDEPPolicy, &DEP_policy, sizeof( DEP_policy ) );
		if ( set_DEP_policy_res == TRUE ) {
			TRACE( _T( "Successfully enabled Permanent DEP, and successfully disabled AtlThunkEmulation.\r\n" ) );
			guard.dismiss( );
			return;
			}
		}


	void enable_EXTENSION_POINT_mitigation( SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f ) {
		PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY EXTEND_POINT_policy = { 0 };
		EXTEND_POINT_policy.DisableExtensionPoints = true;
		
		auto guard = WDS_SCOPEGUARD_INSTANCE( [ &] { handle_mitigation_enable_failure( L"Failed to disable insecure Extension Points: " ); } );

		const BOOL set_EXTEND_policy_res = SetProcessMitigationPolicy_f( ProcessExtensionPointDisablePolicy, &EXTEND_POINT_policy, sizeof( EXTEND_POINT_policy ) );
		if ( set_EXTEND_policy_res == TRUE ) {
			TRACE( _T( "Successfully disabled Extension Points, ancient & insecure extension points are now forbidden.\r\n" ) );
			guard.dismiss( );
			return;
			}
		}

	void enable_strict_HANDLE_check_mitigation( SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f ) {
		PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY HANDLE_policy = { 0 };
		HANDLE_policy.RaiseExceptionOnInvalidHandleReference = true;
		HANDLE_policy.HandleExceptionsPermanentlyEnabled = true;

		auto guard = WDS_SCOPEGUARD_INSTANCE( [&]{ handle_mitigation_enable_failure( L"Failed to enable strict invalid handle checking: " ); } );

		const BOOL set_HANDLE_policy_res = SetProcessMitigationPolicy_f( ProcessStrictHandleCheckPolicy, &HANDLE_policy, sizeof( HANDLE_policy ) );
		if ( set_HANDLE_policy_res == TRUE ) {
			TRACE( _T( "Successfully enabled strict invalid handle checking.\r\n" ) );
			guard.dismiss( );
			return;
			}
		}

	std::pair<const HMODULE, const BOOL> init_kernel32( ) {
		HMODULE kernel32_temp;
		const BOOL module_handle_result = ::GetModuleHandleExW( 0, L"kernel32.dll", &kernel32_temp );
		if ( module_handle_result == 0 ) {
			TRACE( _T( "Failed to get handle to kernel32.dll!\r\n" ) );
			}
		return std::make_pair( kernel32_temp, module_handle_result );
		}

	//Security is a matter determined by the weakest link in the chain. Let's NOT be that link. Let's be respectful of our operating environment.
	void enable_aggressive_process_mitigations( ) {
		
		HMODULE_RAII module_scope_manager { init_kernel32( ) };
		if ( module_scope_manager.result == 0 ) {
			return;
			}
		const SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f = reinterpret_cast< SetProcessMitigationPolicy_t >( GetProcAddress( module_scope_manager.the_module, "SetProcessMitigationPolicy" ) );

		ASSERT( ::IsWindows8OrGreater( ) );
		enable_ASLR_mitigation( SetProcessMitigationPolicy_f );
		
		
		//this one seems to be causing trouble.
		//enable_DEP_mitigation( SetProcessMitigationPolicy_f );
		
		
		enable_EXTENSION_POINT_mitigation( SetProcessMitigationPolicy_f );
		enable_strict_HANDLE_check_mitigation( SetProcessMitigationPolicy_f );
		//TODO:
			//ProhibitDynamicCode: https://msdn.microsoft.com/en-us/library/windows/desktop/mt706243.aspx
			//EnableControlFlowGuard: https://msdn.microsoft.com/en-us/library/windows/desktop/mt654121.aspx
			//

		//(Win 10 only):
			//MicrosoftSignedOnly:https://msdn.microsoft.com/en-us/library/windows/desktop/mt706242.aspx
			//DisableNonSystemFonts: https://msdn.microsoft.com/en-us/library/windows/desktop/mt706244.aspx

		}


	std::wstring test_file_open( ) {
		TRACE( _T( "Displaying shell folder selection dialog...\r\n" ) );
		return OnOpenAFolder( NULL );
		}


	// Get the alternative colors for compressed and encrypted files/folders. This function uses either the value defined in the Explorer configuration or the default color values.
	_Success_(return != clrDefault) COLORREF GetAlternativeColor(_In_ const COLORREF clrDefault, _In_z_ PCWSTR const which) noexcept {
		COLORREF x;
		ULONG cbValue = sizeof(x);
		ATL::CRegKey key;

		// Open the explorer key
		key.Open(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"), KEY_READ);

		// Try to read the REG_BINARY value
		if (ERROR_SUCCESS == key.QueryBinaryValue(which, &x, &cbValue)) {
			return x;
		}
		return clrDefault;
		}

	_Success_(return == true) bool MemoryInfo(_Out_ SIZE_T* m_workingSet) noexcept {
		//auto pmc = zeroInitPROCESS_MEMORY_COUNTERS( );
		PROCESS_MEMORY_COUNTERS pmc = { };

		pmc.cb = sizeof(pmc);

		//GetProcessMemoryInfo function (psapi.h): https://docs.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo
		//If the function succeeds, the return value is nonzero.
		//If the function fails, the return value is zero.
		//To get extended error information, call GetLastError.
		if (!::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc))) {
			return false;
		}

		(*m_workingSet) = pmc.WorkingSetSize;

		return true;
		}

	// Settings used to live under HKCU\Software\Seifert\<profile> (the original WinDirStat author's key).
	// Copy them once to HKCU\Software\altWinDirStat\<profile> so existing users keep their settings.
	// The old key is left in place; nothing is deleted.
	void migrate_legacy_settings_key( _In_z_ PCWSTR const profile_name ) {
		const std::wstring old_path = std::wstring( L"Software\\Seifert\\" ) + profile_name;
		const std::wstring new_path = std::wstring( L"Software\\altWinDirStat\\" ) + profile_name;

		HKEY existing_new = nullptr;
		if ( ::RegOpenKeyExW( HKEY_CURRENT_USER, new_path.c_str( ), 0, KEY_READ, &existing_new ) == ERROR_SUCCESS ) {
			::RegCloseKey( existing_new );
			return; // already migrated (or settings already saved under the new key)
			}
		HKEY old_key = nullptr;
		if ( ::RegOpenKeyExW( HKEY_CURRENT_USER, old_path.c_str( ), 0, KEY_READ, &old_key ) != ERROR_SUCCESS ) {
			return; // nothing to migrate
			}
		const auto old_key_guard = WDS_SCOPEGUARD_INSTANCE( [&] { ::RegCloseKey( old_key ); } );

		HKEY new_key = nullptr;
		const LSTATUS create_res = ::RegCreateKeyExW( HKEY_CURRENT_USER, new_path.c_str( ), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &new_key, nullptr );
		if ( create_res != ERROR_SUCCESS ) {
			TRACE( _T( "Settings migration: couldn't create new key (error %ld)\r\n" ), create_res );
			return;
			}
		const LSTATUS copy_res = ::RegCopyTreeW( old_key, nullptr, new_key );
		::RegCloseKey( new_key );
		if ( copy_res != ERROR_SUCCESS ) {
			// Don't leave a half-copied key behind: it would block a retry next launch. We just created it, so it's ours to remove.
			TRACE( _T( "Settings migration: RegCopyTreeW failed (error %ld)\r\n" ), copy_res );
			::RegDeleteTreeW( HKEY_CURRENT_USER, new_path.c_str( ) );
			::RegDeleteKeyW( HKEY_CURRENT_USER, new_path.c_str( ) );
			}
		}

	bool is_process_elevated( ) noexcept {
		HANDLE token = nullptr;
		if ( !::OpenProcessToken( ::GetCurrentProcess( ), TOKEN_QUERY, &token ) ) {
			return false;
			}
		TOKEN_ELEVATION elevation = { };
		DWORD size = 0;
		const BOOL ok = ::GetTokenInformation( token, TokenElevation, &elevation, sizeof( elevation ), &size );
		::CloseHandle( token );
		return ( ok != FALSE ) && ( elevation.TokenIsElevated != 0 );
		}

	// The folder currently loaded, without the \\?\ prefix, or empty.
	std::wstring current_root_path( ) {
		const CDirstatDoc* const doc = GetDocument( );
		if ( ( doc == nullptr ) || ( doc->m_rootItem == nullptr ) ) {
			return std::wstring( );
			}
		std::wstring path( doc->m_rootItem->GetPath( ) );
		if ( path.compare( 0, 4, L"\\\\?\\" ) == 0 ) {
			path.erase( 0, 4 );
			}
		return path;
		}

	bool is_scanning( ) {
		const CDirstatDoc* const doc = GetDocument( );
		return ( doc != nullptr ) && ( doc->m_rootItem != nullptr ) && !doc->IsRootDone( );
		}

	// Starts another copy of this exe on `path` (may be empty). verb "runas" asks for administrator rights.
	// Returns false if it didn't start, including when the user said No to the UAC prompt.
	bool launch_self( _In_z_ PCWSTR const verb, const std::wstring& path ) {
		wchar_t exe[ MAX_PATH ] = { 0 };
		const DWORD len = ::GetModuleFileNameW( nullptr, exe, MAX_PATH );
		if ( ( len == 0 ) || ( len >= MAX_PATH ) ) {
			return false;
			}
		std::wstring params;
		if ( !path.empty( ) ) {
			// A trailing backslash would escape the closing quote ("C:\" parses as C:"), so double it.
			params = L"\"" + path + ( ( path.back( ) == L'\\' ) ? L"\\" : L"" ) + L"\"";
			}
		SHELLEXECUTEINFOW info = { };
		info.cbSize       = sizeof( info );
		info.fMask        = SEE_MASK_NOASYNC;
		CWnd* const main_wnd = AfxGetMainWnd( ); // null during ExitInstance
		info.hwnd         = ( main_wnd != nullptr ) ? main_wnd->GetSafeHwnd( ) : nullptr;
		info.lpVerb       = verb;
		info.lpFile       = exe;
		info.lpParameters = params.empty( ) ? nullptr : params.c_str( );
		info.nShow        = SW_SHOWNORMAL;
		if ( !::ShellExecuteExW( &info ) ) {
			TRACE( _T( "launch_self(%s) failed: %lu\r\n" ), verb, ::GetLastError( ) );
			return false;
			}
		return true;
		}

	// The start screen: pick a drive (with size/free/used) or a folder. Empty if cancelled.
	std::wstring choose_drive_or_folder( ) {
		CSelectDrivesDlg dlg( AfxGetMainWnd( ) );
		if ( dlg.DoModal( ) != IDOK ) {
			return std::wstring( );
			}
		return dlg.m_selected_path;
		}


	}


// CDirstatApp

BEGIN_MESSAGE_MAP(CDirstatApp, CWinApp)
	ON_COMMAND(ID_APP_ABOUT, &( CDirstatApp::OnAppAbout ) )
	ON_COMMAND(ID_FILE_OPEN, &( CDirstatApp::OnFileOpen ) )
	ON_COMMAND(ID_FILE_NEW, &( CDirstatApp::OnFileOpenLight ) )
	ON_UPDATE_COMMAND_UI(ID_FILE_RESTART_ADMIN, &( CDirstatApp::OnUpdateRestartAdmin ) )
	ON_COMMAND(ID_FILE_RESTART_ADMIN, &( CDirstatApp::OnRestartAdmin ) )
	ON_UPDATE_COMMAND_UI(ID_OPTIONS_RESET_SETTINGS, &( CDirstatApp::OnUpdateResetSettings ) )
	ON_COMMAND(ID_OPTIONS_RESET_SETTINGS, &( CDirstatApp::OnResetSettings ) )
END_MESSAGE_MAP()


WTL::CAppModule _Module;	// add this line
CDirstatApp _theApp;


CDirstatApp::CDirstatApp( ) noexcept : m_workingSet( 0 ), m_lastPeriodicalRamUsageUpdate( ::GetTickCount64( ) ), m_altEncryptionColor( GetAlternativeColor( RGB( 0x00, 0x80, 0x00 ), L"AltEncryptionColor" ) ), m_pDocTemplate(nullptr), m_frameptr(nullptr) { }

CDirstatApp::~CDirstatApp( ) {
	m_pDocTemplate = { NULL };
	}


_Success_( SUCCEEDED( return ) ) HRESULT CDirstatApp::GetCurrentProcessMemoryInfo( _Out_writes_z_( strSize ) _Pre_writable_size_( strSize ) PWSTR psz_formatted_usage, _In_range_( 50, 64 ) const rsize_t strSize ) noexcept {
	const auto Memres = MemoryInfo(&m_workingSet);
	if ( !Memres ) {
		wds_fmt::write_MEM_INFO_ERR( psz_formatted_usage );
		return STRSAFE_E_INVALID_PARAMETER;
		}
	wds_fmt::write_RAM_USAGE( psz_formatted_usage );
	rsize_t chars_written = 0;
	rsize_t size_buff_needed = 0;
	const HRESULT res = wds_fmt::FormatBytes( m_workingSet, &( psz_formatted_usage[ 11 ] ), ( strSize - 12 ), chars_written, size_buff_needed );
	if ( !SUCCEEDED( res ) ) {
		return ::StringCchPrintfW( psz_formatted_usage, strSize, L"RAM Usage: %s", wds_fmt::FormatBytes( m_workingSet, GetOptions( )->m_humanFormat ).c_str( ) );
		}
	return res;
	}

BOOL CDirstatApp::InitInstance( ) {
	//Program entry point

	TRACE( _T( "------>Program entry point!<------\r\n" ) );
	if ( IsWindows8OrGreater( ) ) {
		enable_heap_security_crash_on_corruption( );
		enable_aggressive_process_mitigations( );
		}

	//uses ~29K memory
	if ( !SUCCEEDED( ::CoInitializeEx( NULL, COINIT_APARTMENTTHREADED ) ) ) {
		::AfxMessageBox( _T( "CoInitializeEx Failed!" ) );
		return FALSE;
		}

	
#ifdef DEBUG
	setFlags( );
#endif

	// Initialize ATL
	_Module.Init( NULL, ::AfxGetInstanceHandle( ) );

	VERIFY( CWinApp::InitInstance( ) );
	::InitCommonControls( );          // InitCommonControls() is necessary for Windows XP.
	if ( ::AfxOleInit( ) == FALSE ) { // For SHBrowseForFolder()
		::AfxMessageBox( _T( "AfxOleInit Failed!" ) );
		return FALSE;
		}
	
	

	CWinApp::SetRegistryKey( _T( "altWinDirStat" ) );
	// m_pszProfileName (the app title, "altWinDirStat") is final once SetRegistryKey has run.
	migrate_legacy_settings_key( m_pszProfileName );
	//LoadStdProfileSettings( 4 );

	GetOptions( )->LoadFromRegistry( );
	
	m_pDocTemplate = new CSingleDocTemplate { IDR_MAINFRAME, RUNTIME_CLASS( CDirstatDoc ), RUNTIME_CLASS( CMainFrame ), RUNTIME_CLASS( CGraphView ) };
	if ( !m_pDocTemplate ) {
		return FALSE;
		}

	CWinApp::AddDocTemplate( m_pDocTemplate );
	
	CCommandLineInfo cmdInfo;
	CWinApp::ParseCommandLine( cmdInfo );

	m_nCmdShow = SW_HIDE;


	//Can call OnFileOpen
	if ( !CWinApp::ProcessShellCommand( cmdInfo ) ) {
		return FALSE;
		}

	m_frameptr = GetMainFrame( );
	m_frameptr->m_appptr = this;
	
	
	m_frameptr->InitialShowWindow( );
	m_pMainWnd->UpdateWindow( );

	// When called by setup.exe, windirstat remained in the background, so we do a
	m_pMainWnd->BringWindowToTop( );
	m_pMainWnd->SetForegroundWindow( );

	// No path argument: ProcessShellCommand already routed FileNew -> ID_FILE_NEW -> OnFileOpenLight (the drive picker).
	// Path argument (e.g. `windirstat.exe "D:\"` from the Explorer context menu): ProcessShellCommand already opened and started scanning it.
	// Showing the picker again here would clobber the requested scan, so there's nothing left to do.
	return TRUE;
	}

INT CDirstatApp::ExitInstance( ) {
	if ( m_reset_settings_on_exit ) {
		// Every window has saved its layout by now, so wiping here is final. Leave the key in place (empty) so the
		// one-time Seifert migration doesn't copy the old settings straight back in on the next launch.
		const std::wstring key = std::wstring( L"Software\\altWinDirStat\\" ) + m_pszProfileName;
		const LSTATUS delete_res = ::RegDeleteTreeW( HKEY_CURRENT_USER, key.c_str( ) );
		if ( ( delete_res != ERROR_SUCCESS ) && ( delete_res != ERROR_FILE_NOT_FOUND ) ) {
			TRACE( _T( "Reset settings: RegDeleteTreeW failed (%ld)\r\n" ), delete_res );
			}
		HKEY empty_key = nullptr;
		if ( ::RegCreateKeyExW( HKEY_CURRENT_USER, key.c_str( ), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &empty_key, nullptr ) == ERROR_SUCCESS ) {
			::RegCloseKey( empty_key );
			}
		VERIFY( launch_self( L"open", m_relaunch_path ) );
		}
	// Terminate ATL
	_Module.Term( );
	const auto retval = CWinApp::ExitInstance( );
	return retval;
	}

void CDirstatApp::OnUpdateRestartAdmin( CCmdUI* pCmdUI ) {
	// Already elevated: nothing to restart into.
	pCmdUI->Enable( !is_process_elevated( ) && !is_scanning( ) );
	}

void CDirstatApp::OnRestartAdmin( ) {
	if ( is_process_elevated( ) || is_scanning( ) ) {
		return;
		}
	// If the user declines the UAC prompt, launch_self fails and this window simply stays open.
	if ( launch_self( L"runas", current_root_path( ) ) ) {
		m_pMainWnd->PostMessageW( WM_CLOSE );
		}
	}

void CDirstatApp::OnUpdateResetSettings( CCmdUI* pCmdUI ) {
	pCmdUI->Enable( !is_scanning( ) );
	}

void CDirstatApp::OnResetSettings( ) {
	const int answer = ::MessageBoxW( m_pMainWnd->GetSafeHwnd( ),
		L"Reset all altWinDirStat settings to their defaults?\r\n\r\n"
		L"This resets the window layout, column widths, colors, treemap style and all options. "
		L"altWinDirStat will restart on the same folder.",
		L"altWinDirStat - Reset All Settings", MB_YESNO bitor MB_ICONQUESTION bitor MB_DEFBUTTON2 );
	if ( answer != IDYES ) {
		return;
		}
	m_reset_settings_on_exit = true;
	m_relaunch_path = current_root_path( );
	m_pMainWnd->PostMessageW( WM_CLOSE );
	}

void CDirstatApp::OnAppAbout( ) {
	displayWindowsMsgBoxWithMessage( global_strings::about_text );
	}

void CDirstatApp::OnFileOpen( ) {
	if ( is_scanning( ) ) {
		return;
		}
	const auto path_str = test_file_open( );
	if ( !( path_str.empty( ) ) ) {
		m_pDocTemplate->OpenDocumentFile( path_str.c_str( ), true );
		}
	}

// Start screen (also Ctrl+O): pick a drive from the list, or a folder.
void CDirstatApp::OnFileOpenLight( ) {
	if ( is_scanning( ) ) {
		return;
		}
	const auto path_str = choose_drive_or_folder( );
	if ( !( path_str.empty( ) ) ) {
		//Here, calls CSingleDocTemplate::OpenDocumentFile (in docsingl.cpp). Safe with a tree already loaded: see CDirstatDoc::DeleteContents.
		m_pDocTemplate->OpenDocumentFile( path_str.c_str( ), TRUE );
		}
	}

BOOL CDirstatApp::OnIdle( _In_ LONG lCount ) {
	BOOL more = FALSE;
	ASSERT( lCount >= 0 );
	const auto ramDiff = ( ::GetTickCount64( ) - m_lastPeriodicalRamUsageUpdate );
	auto doc = GetDocument( );
	
	if ( doc != nullptr ) {
		if ( !doc->Work( ) ) {
			//ASSERT( doc->m_workingItem != NULL );
			more = TRUE;
			}
		}
	if ( ramDiff > RAM_USAGE_UPDATE_INTERVAL ) {
		more = CWinApp::OnIdle( lCount );
		if ( !more ) {
			CDirstatApp::PeriodicalUpdateRamUsage( );
			}
		else {
			more = CWinThread::OnIdle( 0 );
			}
		}
	return more;
	}


#else

#endif