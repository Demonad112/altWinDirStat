// selectdrivesdlg.h	- Declaration of CSelectDrivesDlg, the "what do you want to scan?" start screen
//
// see `file_header_text.txt` for licensing & contact info. If you can't find that file, then assume you're NOT allowed to do whatever you wanted to do.

#pragma once

#include "stdafx.h"
#include "macros_that_scare_small_children.h"

#ifndef WDS_SELECTDRIVESDLG_H
#define WDS_SELECTDRIVESDLG_H

WDS_FILE_INCLUDE_MESSAGE

// Lists every drive with its size, free space and a used-space bar. The user picks a drive (double-click or "Scan Drive"),
// or "Choose a Folder..." for the shell folder picker. On IDOK, m_selected_path holds the path to scan.
class CSelectDrivesDlg final : public CDialog {
	DISALLOW_COPY_AND_ASSIGN( CSelectDrivesDlg );
public:
	CSelectDrivesDlg( _In_opt_ CWnd* const parent ) : CDialog( IDD_SELECTDRIVES, parent ) { }

	std::wstring m_selected_path;

protected:
	struct drive_row final {
		std::wstring   root;       // "C:\"
		std::uint64_t  total;
		std::uint64_t  free;
		bool           available;  // false: no media / disconnected network drive
		};

	virtual BOOL OnInitDialog( ) override final;
	virtual void OnOK( ) override final;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBrowseFolder( );
	afx_msg void OnDblclkDrives( NMHDR* pNMHDR, LRESULT* pResult );
	afx_msg void OnItemchangedDrives( NMHDR* pNMHDR, LRESULT* pResult );
	afx_msg void OnCustomDrawDrives( NMHDR* pNMHDR, LRESULT* pResult );

private:
	void fill_drive_list( );
	void update_scan_button( );
	_Ret_maybenull_ const drive_row* selected_row( ) const;

	CListCtrl              m_drives;
	std::vector<drive_row> m_rows;
	};

#else

#endif
