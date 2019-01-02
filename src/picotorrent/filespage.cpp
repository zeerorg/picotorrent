#include "filespage.hpp"

#include "filecontextmenu.hpp"
#include "filestorageviewmodel.hpp"
#include "string.hpp"
#include "translator.hpp"
#include "scaler.hpp"

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <wx/dataview.h>

namespace lt = libtorrent;
using pt::FilesPage;

struct FilesPage::HandleWrapper
{
    lt::torrent_handle handle;
};

wxBEGIN_EVENT_TABLE(FilesPage, wxPanel)
    EVT_MENU(pt::FileContextMenu::ptID_PRIO_MAXIMUM, FilesPage::OnSetPriority)
    EVT_MENU(pt::FileContextMenu::ptID_PRIO_LOW, FilesPage::OnSetPriority)
    EVT_MENU(pt::FileContextMenu::ptID_PRIO_NORMAL, FilesPage::OnSetPriority)
    EVT_MENU(pt::FileContextMenu::ptID_PRIO_DO_NOT_DOWNLOAD, FilesPage::OnSetPriority)
    EVT_DATAVIEW_ITEM_CONTEXT_MENU(ptID_FILE_LIST, FilesPage::OnFileContextMenu)
wxEND_EVENT_TABLE()

FilesPage::FilesPage(wxWindow* parent, wxWindowID id, std::shared_ptr<pt::Translator> tran)
    : wxPanel(parent, id),
    m_trans(tran),
    m_wrapper(std::make_unique<HandleWrapper>()),
    m_filesView(new wxDataViewCtrl(this, ptID_FILE_LIST, wxDefaultPosition, wxDefaultSize, wxDV_MULTIPLE)),
    m_viewModel(new FileStorageViewModel(tran))
{
    auto nameCol = m_filesView->AppendIconTextColumn(
        i18n(tran, "name"),
        FileStorageViewModel::Columns::Name,
        wxDATAVIEW_CELL_INERT,
        SX(220),
        wxALIGN_LEFT);

    m_filesView->AppendTextColumn(
        i18n(tran, "size"),
        FileStorageViewModel::Columns::Size,
        wxDATAVIEW_CELL_INERT,
        SX(80),
        wxALIGN_RIGHT);

    m_filesView->AppendProgressColumn(
        i18n(tran, "progress"),
        FileStorageViewModel::Columns::Progress,
        wxDATAVIEW_CELL_INERT,
        SX(80));

    m_filesView->AppendTextColumn(
        i18n(tran, "priority"),
        FileStorageViewModel::Columns::Priority,
        wxDATAVIEW_CELL_INERT,
        SX(80),
        wxALIGN_RIGHT);

    // Ugly hack to prevent the last "real" column from stretching.
    m_filesView->AppendColumn(new wxDataViewColumn(wxEmptyString, new wxDataViewTextRenderer(), -1, 0));

    nameCol->GetRenderer()->EnableEllipsize(wxELLIPSIZE_END);

    m_filesView->AssociateModel(m_viewModel);
    m_viewModel->DecRef();

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_filesView, 1, wxEXPAND);

    this->SetSizer(sizer);
}

void FilesPage::Clear()
{
    m_wrapper->handle = lt::torrent_handle();

    m_viewModel->ClearNodes();
    m_viewModel->Cleared();
}

void FilesPage::Update(lt::torrent_status const& ts)
{
    if (!ts.handle.is_valid())
    {
        return;
    }

    std::shared_ptr<const lt::torrent_info> tf = ts.torrent_file.lock();
    if (!tf) { return; }

    m_filesView->Freeze();

    if (m_wrapper->handle.info_hash() != ts.info_hash)
    {
        m_wrapper->handle = ts.handle;
        m_viewModel->RebuildTree(tf);
        m_filesView->Expand(m_viewModel->GetRootItem());
    }

    std::vector<int64_t> progress;
    ts.handle.file_progress(progress);

    m_viewModel->UpdatePriorities(m_wrapper->handle.get_file_priorities());
    m_viewModel->UpdateProgress(progress);

    m_filesView->Thaw();
}

void FilesPage::OnFileContextMenu(wxDataViewEvent& event)
{
    if (!event.GetItem().IsOk())
    {
        return;
    }

    FileContextMenu menu(m_trans);
    PopupMenu(&menu);
}

void FilesPage::OnSetPriority(wxCommandEvent& event)
{
    wxDataViewItemArray items;
    m_filesView->GetSelections(items);
    std::vector<int> fileIndices;
    for (auto item : items)
    {
        std::vector<int> indices = m_viewModel->GetFileIndices(item);
        fileIndices.insert(fileIndices.end(), indices.begin(), indices.end());
    }

    for (auto index : fileIndices)
    {
        switch (event.GetId())
        {
        case FileContextMenu::ptID_PRIO_MAXIMUM:
            m_wrapper->handle.file_priority(lt::file_index_t(index), lt::top_priority);
            break;
        case FileContextMenu::ptID_PRIO_NORMAL:
            m_wrapper->handle.file_priority(lt::file_index_t(index), lt::default_priority);
            break;
        case FileContextMenu::ptID_PRIO_LOW:
            m_wrapper->handle.file_priority(lt::file_index_t(index), lt::low_priority);
            break;
        case FileContextMenu::ptID_PRIO_DO_NOT_DOWNLOAD:
            m_wrapper->handle.file_priority(lt::file_index_t(index), lt::dont_download);
            break;
        }
    }

    m_viewModel->UpdatePriorities(m_wrapper->handle.get_file_priorities());
}
