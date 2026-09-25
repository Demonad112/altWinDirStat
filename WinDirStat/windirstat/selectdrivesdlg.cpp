// selectdrivesdlg.cpp	- Implementation of CSelectDrivesDlg, the "what do you want to scan?" start screen
//
// see `file_header_text.txt` for licensing & contact info. If you can't find that file, then assume you're NOT allowed to do whatever you wanted to do.
#include "stdafx.h"

#pragma once

#ifndef WDS_SELECTDRIVESDLG_CPP
#define WDS_SELECTDRIVESDLG_CPP

WDS_FILE_INCLUDE_MESSAGE

#include "selectdrivesdlg.h"
#include "stringformatting.h"
#include "COM_helpers.h"

namespace {
	enum drive_column : int {
		COL_DRIVE = 0,
		COL_TYPE,
		COL_SIZE,
		COL_FREE,
		COL_USED
		};

	PCWSTR drive_type_name( const UINT type ) noexcept {
		switch ( type ) {
				case DRIVE_REMOVABLE: return L"Removable (USB)";
				case DRIVE_FIXED:     return L"Local disk";
				case DRIVE_REMOTE:    return L"Network drive";
				case DRIVE_CDROM:     return L"CD/DVD";
				case DRIVE_RAMDISK:   return L"RAM disk";
				default:              return L"Other";
			}
		}

	// "C:\  Windows" (label shown when the volume has one).
	std::wstring drive_display_name( const std::wstring& root ) {
		wchar_t label[ MAX_PATH + 1 ] = { 0 };
		std::wstring name( root );
		if ( ::GetVolumeInformationW( root.c_str( ), label, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0 ) && ( label[ 0 ] != L'\0' ) ) {
			name += L"  ";
			name += label;
			}
		return name;
		}

	// Share of the drive in use, 0..100, or -1 when it can't be read.
	int used_percent( const std::uint64_t total, const std::uint64_t free ) noexcept {
		if ( total == 0 ) {
			return -1;
			}
		return static_cast<int>( ( ( total - free ) * 100u ) / total );
		}
	}

BEGIN_MESSAGE_MAP( CSelectDrivesDlg, CDialog )
	ON_BN_CLICKED( IDC_BROWSEFOLDER, &( CSelectDrivesDlg::OnBrowseFolder ) )
	ON_NOTIFY( NM_DBLCLK, IDC_DRIVES, &( CSelectDrivesDlg::OnDblclkDrives ) )
	ON_NOTIFY( LVN_ITEMCHANGED, IDC_DRIVES, &( CSelectDrivesDlg::OnItemchangedDrives ) )
	ON_NOTIFY( NM_CUSTOMDRAW, IDC_DRIVES, &( CSelectDrivesDlg::OnCustomDrawDrives ) )
END_MESSAGE_MAP( )

BOOL CSelectDrivesDlg::OnInitDialog( ) {
	VERIFY( CDialog::OnInitDialog( ) );
	VERIFY( m_drives.SubclassDlgItem( IDC_DRIVES, this ) );
	m_drives.SetExtendedStyle( m_drives.GetExtendedStyle( ) bitor LVS_EX_FULLROWSELECT bitor LVS_EX_DOUBLEBUFFER );

	m_drives.InsertColumn( COL_DRIVE, L"Drive",      LVCFMT_LEFT,  150 );
	m_drives.InsertColumn( COL_TYPE,  L"Type",       LVCFMT_LEFT,  105 );
	m_drives.InsertColumn( COL_SIZE,  L"Size",       LVCFMT_RIGHT,  80 );
	m_drives.InsertColumn( COL_FREE,  L"Free",       LVCFMT_RIGHT,  80 );
	m_drives.InsertColumn( COL_USED,  L"Used",       LVCFMT_LEFT,  110 );

	fill_drive_list( );

	// Preselect the first drive that can be scanned (usually C:), so Enter scans straight away.
	for ( size_t i = 0; i < m_rows.size( ); ++i ) {
		if ( m_rows[ i ].available ) {
			m_drives.SetItemState( static_cast<int>( i ), LVIS_SELECTED bitor LVIS_FOCUSED, LVIS_SELECTED bitor LVIS_FOCUSED );
			break;
			}
		}
	update_scan_button( );
	m_drives.SetFocus( );
	return FALSE; // focus set above
	}

void CSelectDrivesDlg::fill_drive_list( ) {
	m_rows.clear( );
	VERIFY( m_drives.DeleteAllItems( ) );

	wchar_t buffer[ 512 ] = { 0 };
	const DWORD len = ::GetLogicalDriveStringsW( static_cast<DWORD>( _countof( buffer ) - 1 ), buffer );
	if ( ( len == 0 ) || ( len >= _countof( buffer ) ) ) {
		TRACE( _T( "GetLogicalDriveStringsW failed or buffer too small (%lu)\r\n" ), len );
		return;
		}

	// Drives that can't be read (empty card reader, disconnected network share) stay out of the way at the bottom.
	std::vector<drive_row> unavailable;
	for ( PCWSTR root = buffer; *root != L'\0'; root += ( ::wcslen( root ) + 1 ) ) {
		const UINT type = ::GetDriveTypeW( root );
		if ( ( type == DRIVE_UNKNOWN ) || ( type == DRIVE_NO_ROOT_DIR ) ) {
			continue;
			}
		ULARGE_INTEGER free_to_caller = { }, total = { }, total_free = { };
		const bool available = ( ::GetDiskFreeSpaceExW( root, &free_to_caller, &total, &total_free ) != FALSE );
		drive_row row { root, available ? total.QuadPart : 0u, available ? total_free.QuadPart : 0u, available };
		( available ? m_rows : unavailable ).push_back( std::move( row ) );
		}
	m_rows.insert( m_rows.end( ), unavailable.begin( ), unavailable.end( ) );

	for ( size_t i = 0; i < m_rows.size( ); ++i ) {
		const auto& row = m_rows[ i ];
		const int item = m_drives.InsertItem( static_cast<int>( i ), row.available ? drive_display_name( row.root ).c_str( ) : row.root.c_str( ) );
		m_drives.SetItemText( item, COL_TYPE, drive_type_name( ::GetDriveTypeW( row.root.c_str( ) ) ) );
		if ( row.available ) {
			m_drives.SetItemText( item, COL_SIZE, wds_fmt::FormatBytes( row.total, true ).c_str( ) );
			m_drives.SetItemText( item, COL_FREE, wds_fmt::FormatBytes( row.free, true ).c_str( ) );
			}
		else {
			m_drives.SetItemText( item, COL_SIZE, L"-" );
			m_drives.SetItemText( item, COL_FREE, L"-" );
			m_drives.SetItemText( item, COL_USED, L"Not available" );
			}
		}
	}

_Ret_maybenull_ const CSelectDrivesDlg::drive_row* CSelectDrivesDlg::selected_row( ) const {
	const int item = m_drives.GetNextItem( -1, LVNI_SELECTED );
	if ( ( item < 0 ) || ( static_cast<size_t>( item ) >= m_rows.size( ) ) ) {
		return nullptr;
		}
	return &( m_rows[ static_cast<size_t>( item ) ] );
	}

void CSelectDrivesDlg::update_scan_button( ) {
	const auto row = selected_row( );
	CWnd* const scan_button = GetDlgItem( IDOK );
	if ( scan_button != nullptr ) {
		scan_button->EnableWindow( ( row != nullptr ) && row->available );
		}
	}

void CSelectDrivesDlg::OnOK( ) {
	const auto row = selected_row( );
	if ( ( row == nullptr ) || !row->available ) {
		return;
		}
	m_selected_path = row->root;
	CDialog::OnOK( );
	}

void CSelectDrivesDlg::OnBrowseFolder( ) {
	const std::wstring folder = OnOpenAFolder( m_hWnd );
	if ( folder.empty( ) ) {
		return; // cancelled: stay on this screen
		}
	m_selected_path = folder;
	CDialog::OnOK( );
	}

void CSelectDrivesDlg::OnDblclkDrives( NMHDR* /*pNMHDR*/, LRESULT* pResult ) {
	*pResult = 0;
	OnOK( );
	}

void CSelectDrivesDlg::OnItemchangedDrives( NMHDR* /*pNMHDR*/, LRESULT* pResult ) {
	*pResult = 0;
	update_scan_button( );
	}

// Draws the "Used" column as a bar with the percentage on it; red once a drive is 90% full.
void CSelectDrivesDlg::OnCustomDrawDrives( NMHDR* pNMHDR, LRESULT* pResult ) {
	NMLVCUSTOMDRAW* const draw = reinterpret_cast<NMLVCUSTOMDRAW*>( pNMHDR );
	*pResult = CDRF_DODEFAULT;
	switch ( draw->nmcd.dwDrawStage ) {
			case CDDS_PREPAINT:
				*pResult = CDRF_NOTIFYITEMDRAW;
				return;
			case CDDS_ITEMPREPAINT:
				*pResult = CDRF_NOTIFYSUBITEMDRAW;
				return;
			case ( CDDS_ITEMPREPAINT bitor CDDS_SUBITEM ):
				break;
			default:
				return;
		}
	const auto item = static_cast<size_t>( draw->nmcd.dwItemSpec );
	if ( ( draw->iSubItem != COL_USED ) || ( item >= m_rows.size( ) ) || !m_rows[ item ].available ) {
		return;
		}
	const int percent = used_percent( m_rows[ item ].total, m_rows[ item ].free );
	if ( percent < 0 ) {
		return;
		}

	CRect cell;
	if ( !m_drives.GetSubItemRect( static_cast<int>( item ), COL_USED, LVIR_BOUNDS, cell ) ) {
		return;
		}
	CDC* const dc = CDC::FromHandle( draw->nmcd.hdc );
	const bool selected = ( m_drives.GetItemState( static_cast<int>( item ), LVIS_SELECTED ) bitand LVIS_SELECTED ) != 0;
	dc->FillSolidRect( cell, ::GetSysColor( selected ? COLOR_HIGHLIGHT : COLOR_WINDOW ) );

	CRect bar( cell );
	bar.DeflateRect( 4, 3 );
	dc->FillSolidRect( bar, RGB( 0xE4, 0xE6, 0xEF ) );
	CRect filled( bar );
	filled.right = bar.left + ( ( bar.Width( ) * percent ) / 100 );
	dc->FillSolidRect( filled, ( percent >= 90 ) ? RGB( 0xC6, 0x3B, 0x2E ) : RGB( 0x4B, 0x4F, 0xD6 ) );
	dc->Draw3dRect( bar, RGB( 0xB8, 0xBC, 0xCC ), RGB( 0xB8, 0xBC, 0xCC ) );

	const std::wstring label = std::to_wstring( percent ) + L"% used";
	const int old_mode = dc->SetBkMode( TRANSPARENT );
	const COLORREF old_color = dc->SetTextColor( ( percent >= 50 ) ? RGB( 0xFF, 0xFF, 0xFF ) : RGB( 0x1A, 0x1D, 0x2C ) );
	dc->DrawTextW( label.c_str( ), static_cast<int>( label.length( ) ), bar, DT_CENTER bitor DT_VCENTER bitor DT_SINGLELINE bitor DT_NOPREFIX );
	dc->SetTextColor( old_color );
	dc->SetBkMode( old_mode );
	*pResult = CDRF_SKIPDEFAULT;
	}

#else

#endif
