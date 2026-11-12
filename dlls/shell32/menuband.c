/*
 * Menu band (CLSID_MenuBand)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * This implements a horizontal menu bar built from an HMENU, as used by
 * applications that host a menu band in their own rebar: each top-level
 * item becomes a toolbar button, and pressing it opens the item's submenu
 * with commands sent to the owner window. Menus built from shell folders
 * and vertical (popup) menu bands are not implemented.
 */

#define COBJMACROS

#include "shobjidl.h"
#include "shlobj.h"
#include "commctrl.h"

#include "shell32_main.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

struct menu_band
{
    IShellMenu IShellMenu_iface;
    IDeskBand IDeskBand_iface;
    IObjectWithSite IObjectWithSite_iface;
    IMenuBand IMenuBand_iface;
    IWinEventHandler IWinEventHandler_iface;
    IInputObject IInputObject_iface;
    LONG refcount;

    IShellMenuCallback *callback;
    UINT id, ancestor_id;
    DWORD init_flags;

    HMENU menu;
    HWND owner;
    DWORD menu_flags;

    IUnknown *site;
    HWND window, toolbar;
};

static const WCHAR menu_band_classW[] = L"WineMenuBand";

static struct menu_band *impl_from_IShellMenu(IShellMenu *iface)
{
    return CONTAINING_RECORD(iface, struct menu_band, IShellMenu_iface);
}

static struct menu_band *impl_from_IDeskBand(IDeskBand *iface)
{
    return CONTAINING_RECORD(iface, struct menu_band, IDeskBand_iface);
}

static struct menu_band *impl_from_IObjectWithSite(IObjectWithSite *iface)
{
    return CONTAINING_RECORD(iface, struct menu_band, IObjectWithSite_iface);
}

static struct menu_band *impl_from_IMenuBand(IMenuBand *iface)
{
    return CONTAINING_RECORD(iface, struct menu_band, IMenuBand_iface);
}

static struct menu_band *impl_from_IWinEventHandler(IWinEventHandler *iface)
{
    return CONTAINING_RECORD(iface, struct menu_band, IWinEventHandler_iface);
}

static struct menu_band *impl_from_IInputObject(IInputObject *iface)
{
    return CONTAINING_RECORD(iface, struct menu_band, IInputObject_iface);
}

/* Fills the toolbar with one button per top-level menu item. */
static void menu_band_fill_toolbar(struct menu_band *band)
{
    int count, i;

    if (!band->toolbar)
        return;

    while (SendMessageW(band->toolbar, TB_DELETEBUTTON, 0, 0))
        ;

    if (!band->menu || (count = GetMenuItemCount(band->menu)) <= 0)
        return;

    for (i = 0; i < count; ++i)
    {
        MENUITEMINFOW info = { sizeof(info) };
        TBBUTTON button = { 0 };
        WCHAR text[256];

        info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_STRING;
        info.dwTypeData = text;
        info.cch = ARRAY_SIZE(text);
        if (!GetMenuItemInfoW(band->menu, i, TRUE, &info))
            continue;
        if (info.fType & MFT_SEPARATOR)
            continue;
        if (!info.cch)
            text[0] = 0;

        button.iBitmap = I_IMAGENONE;
        button.idCommand = i + 1;
        button.fsState = (info.fState & MFS_DISABLED) ? 0 : TBSTATE_ENABLED;
        button.fsStyle = BTNS_DROPDOWN | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        button.iString = (INT_PTR)text;
        SendMessageW(band->toolbar, TB_ADDBUTTONSW, 1, (LPARAM)&button);
    }
    SendMessageW(band->toolbar, TB_AUTOSIZE, 0, 0);
}

static void menu_band_open_item(struct menu_band *band, int command)
{
    int index = command - 1;
    HMENU submenu;
    RECT rect;

    TRACE("band %p, index %d.\n", band, index);

    if (!(submenu = GetSubMenu(band->menu, index)))
    {
        UINT id = GetMenuItemID(band->menu, index);

        if (id != ~0u)
            PostMessageW(band->owner, WM_COMMAND, MAKEWPARAM(id, 0), 0);
        return;
    }

    SendMessageW(band->toolbar, TB_GETRECT, command, (LPARAM)&rect);
    MapWindowPoints(band->toolbar, NULL, (POINT *)&rect, 2);

    SendMessageW(band->toolbar, TB_PRESSBUTTON, command, TRUE);
    TrackPopupMenuEx(submenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_VERTICAL,
            rect.left, rect.bottom, band->owner, NULL);
    SendMessageW(band->toolbar, TB_PRESSBUTTON, command, FALSE);
}

static LRESULT CALLBACK menu_band_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct menu_band *band = (struct menu_band *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lparam)->lpCreateParams);
        break;

    case WM_SIZE:
        if (band && band->toolbar)
            MoveWindow(band->toolbar, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        return 0;

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lparam;

        if (band && hdr->hwndFrom == band->toolbar && hdr->code == TBN_DROPDOWN)
        {
            menu_band_open_item(band, ((NMTOOLBARW *)lparam)->iItem);
            return TBDDRET_DEFAULT;
        }
        break;
    }

    case WM_DESTROY:
        if (band)
        {
            band->window = NULL;
            band->toolbar = NULL;
        }
        break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void menu_band_register_class(void)
{
    WNDCLASSW class = { 0 };
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };

    InitCommonControlsEx(&icc);

    class.lpfnWndProc = menu_band_wndproc;
    class.hInstance = shell32_hInstance;
    class.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    class.hbrBackground = (HBRUSH)(COLOR_MENUBAR + 1);
    class.lpszClassName = menu_band_classW;
    RegisterClassW(&class);
}

static HWND menu_band_get_parent(struct menu_band *band)
{
    IOleWindow *ole_window;
    HWND parent = NULL;

    if (band->site && SUCCEEDED(IUnknown_QueryInterface(band->site, &IID_IOleWindow, (void **)&ole_window)))
    {
        IOleWindow_GetWindow(ole_window, &parent);
        IOleWindow_Release(ole_window);
    }
    return parent ? parent : band->owner;
}

static HRESULT menu_band_create_window(struct menu_band *band)
{
    HWND parent;

    if (band->window)
        return S_OK;
    if (!(parent = menu_band_get_parent(band)))
        return E_FAIL;

    menu_band_register_class();

    band->window = CreateWindowExW(0, menu_band_classW, NULL, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            0, 0, 0, 0, parent, NULL, shell32_hInstance, band);
    if (!band->window)
        return HRESULT_FROM_WIN32(GetLastError());

    band->toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT
            | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE,
            0, 0, 0, 0, band->window, NULL, shell32_hInstance, NULL);
    if (!band->toolbar)
    {
        DestroyWindow(band->window);
        return E_FAIL;
    }
    SendMessageW(band->toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(band->toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));

    menu_band_fill_toolbar(band);
    TRACE("band %p, created window %p, toolbar %p, parent %p.\n", band, band->window, band->toolbar, parent);
    return S_OK;
}

static void menu_band_set_menu(struct menu_band *band, HMENU menu, HWND owner, DWORD flags)
{
    if (band->menu && band->menu != menu && !(band->menu_flags & SMSET_DONTOWN))
        DestroyMenu(band->menu);
    band->menu = menu;
    band->owner = owner;
    band->menu_flags = flags;
    menu_band_fill_toolbar(band);
}

static HRESULT WINAPI shell_menu_QueryInterface(IShellMenu *iface, REFIID iid, void **out)
{
    struct menu_band *band = impl_from_IShellMenu(iface);

    TRACE("band %p, iid %s, out %p.\n", band, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IShellMenu))
        *out = &band->IShellMenu_iface;
    else if (IsEqualGUID(iid, &IID_IOleWindow) || IsEqualGUID(iid, &IID_IDockingWindow)
            || IsEqualGUID(iid, &IID_IDeskBand))
        *out = &band->IDeskBand_iface;
    else if (IsEqualGUID(iid, &IID_IObjectWithSite))
        *out = &band->IObjectWithSite_iface;
    else if (IsEqualGUID(iid, &IID_IMenuBand))
        *out = &band->IMenuBand_iface;
    else if (IsEqualGUID(iid, &IID_IWinEventHandler))
        *out = &band->IWinEventHandler_iface;
    else if (IsEqualGUID(iid, &IID_IInputObject))
        *out = &band->IInputObject_iface;
    else
    {
        *out = NULL;
        FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI shell_menu_AddRef(IShellMenu *iface)
{
    struct menu_band *band = impl_from_IShellMenu(iface);
    ULONG refcount = InterlockedIncrement(&band->refcount);

    TRACE("%p increasing refcount to %lu.\n", band, refcount);
    return refcount;
}

static ULONG WINAPI shell_menu_Release(IShellMenu *iface)
{
    struct menu_band *band = impl_from_IShellMenu(iface);
    ULONG refcount = InterlockedDecrement(&band->refcount);

    TRACE("%p decreasing refcount to %lu.\n", band, refcount);

    if (!refcount)
    {
        if (band->window)
            DestroyWindow(band->window);
        if (band->site)
            IUnknown_Release(band->site);
        if (band->callback)
            IShellMenuCallback_Release(band->callback);
        menu_band_set_menu(band, NULL, NULL, 0);
        free(band);
    }
    return refcount;
}

static HRESULT WINAPI shell_menu_Initialize(IShellMenu *iface, IShellMenuCallback *callback,
        UINT id, UINT ancestor_id, DWORD flags)
{
    struct menu_band *band = impl_from_IShellMenu(iface);

    TRACE("band %p, callback %p, id %u, ancestor_id %u, flags %#lx.\n", band, callback, id, ancestor_id, flags);

    if (!(flags & SMINIT_HORIZONTAL))
        FIXME("Vertical menu bands are not implemented.\n");

    if (callback)
        IShellMenuCallback_AddRef(callback);
    if (band->callback)
        IShellMenuCallback_Release(band->callback);
    band->callback = callback;
    band->id = id;
    band->ancestor_id = ancestor_id;
    band->init_flags = flags;
    return S_OK;
}

static HRESULT WINAPI shell_menu_GetMenuInfo(IShellMenu *iface, IShellMenuCallback **callback,
        UINT *id, UINT *ancestor_id, DWORD *flags)
{
    struct menu_band *band = impl_from_IShellMenu(iface);

    TRACE("band %p, callback %p, id %p, ancestor_id %p, flags %p.\n", band, callback, id, ancestor_id, flags);

    if (callback)
    {
        if ((*callback = band->callback))
            IShellMenuCallback_AddRef(*callback);
    }
    if (id)
        *id = band->id;
    if (ancestor_id)
        *ancestor_id = band->ancestor_id;
    if (flags)
        *flags = band->init_flags;
    return S_OK;
}

static HRESULT WINAPI shell_menu_SetShellFolder(IShellMenu *iface, IShellFolder *folder,
        LPCITEMIDLIST pidl, HKEY key, DWORD flags)
{
    FIXME("iface %p, folder %p, pidl %p, key %p, flags %#lx, stub!\n", iface, folder, pidl, key, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI shell_menu_GetShellFolder(IShellMenu *iface, DWORD *flags, LPITEMIDLIST *pidl,
        REFIID iid, void **out)
{
    FIXME("iface %p, flags %p, pidl %p, iid %s, out %p, stub!\n", iface, flags, pidl, debugstr_guid(iid), out);
    return E_NOTIMPL;
}

static HRESULT WINAPI shell_menu_SetMenu(IShellMenu *iface, HMENU menu, HWND owner, DWORD flags)
{
    struct menu_band *band = impl_from_IShellMenu(iface);

    TRACE("band %p, menu %p, owner %p, flags %#lx.\n", band, menu, owner, flags);

    menu_band_set_menu(band, menu, owner, flags);
    return S_OK;
}

static HRESULT WINAPI shell_menu_GetMenu(IShellMenu *iface, HMENU *menu, HWND *owner, DWORD *flags)
{
    struct menu_band *band = impl_from_IShellMenu(iface);

    TRACE("band %p, menu %p, owner %p, flags %p.\n", band, menu, owner, flags);

    if (menu)
        *menu = band->menu;
    if (owner)
        *owner = band->owner;
    if (flags)
        *flags = band->menu_flags;
    return S_OK;
}

static HRESULT WINAPI shell_menu_InvalidateItem(IShellMenu *iface, LPSMDATA data, DWORD flags)
{
    struct menu_band *band = impl_from_IShellMenu(iface);

    TRACE("band %p, data %p, flags %#lx.\n", band, data, flags);

    menu_band_fill_toolbar(band);
    return S_OK;
}

static HRESULT WINAPI shell_menu_GetState(IShellMenu *iface, LPSMDATA data)
{
    FIXME("iface %p, data %p, stub!\n", iface, data);
    return E_NOTIMPL;
}

static HRESULT WINAPI shell_menu_SetMenuToolbar(IShellMenu *iface, IUnknown *toolbar, DWORD flags)
{
    FIXME("iface %p, toolbar %p, flags %#lx, stub!\n", iface, toolbar, flags);
    return E_NOTIMPL;
}

static const IShellMenuVtbl shell_menu_vtbl =
{
    shell_menu_QueryInterface,
    shell_menu_AddRef,
    shell_menu_Release,
    shell_menu_Initialize,
    shell_menu_GetMenuInfo,
    shell_menu_SetShellFolder,
    shell_menu_GetShellFolder,
    shell_menu_SetMenu,
    shell_menu_GetMenu,
    shell_menu_InvalidateItem,
    shell_menu_GetState,
    shell_menu_SetMenuToolbar,
};

static HRESULT WINAPI desk_band_QueryInterface(IDeskBand *iface, REFIID iid, void **out)
{
    struct menu_band *band = impl_from_IDeskBand(iface);
    return IShellMenu_QueryInterface(&band->IShellMenu_iface, iid, out);
}

static ULONG WINAPI desk_band_AddRef(IDeskBand *iface)
{
    struct menu_band *band = impl_from_IDeskBand(iface);
    return IShellMenu_AddRef(&band->IShellMenu_iface);
}

static ULONG WINAPI desk_band_Release(IDeskBand *iface)
{
    struct menu_band *band = impl_from_IDeskBand(iface);
    return IShellMenu_Release(&band->IShellMenu_iface);
}

static HRESULT WINAPI desk_band_GetWindow(IDeskBand *iface, HWND *window)
{
    struct menu_band *band = impl_from_IDeskBand(iface);
    HRESULT hr;

    TRACE("band %p, window %p.\n", band, window);

    if (!window)
        return E_INVALIDARG;
    if (FAILED(hr = menu_band_create_window(band)))
    {
        *window = NULL;
        return hr;
    }
    *window = band->window;
    return S_OK;
}

static HRESULT WINAPI desk_band_ContextSensitiveHelp(IDeskBand *iface, BOOL enter)
{
    TRACE("iface %p, enter %d.\n", iface, enter);
    return E_NOTIMPL;
}

static HRESULT WINAPI desk_band_ShowDW(IDeskBand *iface, BOOL show)
{
    struct menu_band *band = impl_from_IDeskBand(iface);

    TRACE("band %p, show %d.\n", band, show);

    if (show && FAILED(menu_band_create_window(band)))
        return E_FAIL;
    if (band->window)
        ShowWindow(band->window, show ? SW_SHOW : SW_HIDE);
    return S_OK;
}

static HRESULT WINAPI desk_band_CloseDW(IDeskBand *iface, DWORD reserved)
{
    struct menu_band *band = impl_from_IDeskBand(iface);

    TRACE("band %p, reserved %#lx.\n", band, reserved);

    if (band->window)
        DestroyWindow(band->window);
    return S_OK;
}

static HRESULT WINAPI desk_band_ResizeBorderDW(IDeskBand *iface, const RECT *border,
        IUnknown *site, BOOL reserved)
{
    TRACE("iface %p, border %s, site %p, reserved %d.\n", iface, wine_dbgstr_rect(border), site, reserved);
    return E_NOTIMPL;
}

static HRESULT WINAPI desk_band_GetBandInfo(IDeskBand *iface, DWORD band_id, DWORD mode, DESKBANDINFO *info)
{
    struct menu_band *band = impl_from_IDeskBand(iface);
    SIZE size = { 0 };

    TRACE("band %p, band_id %lu, mode %#lx, info %p.\n", band, band_id, mode, info);

    if (!info)
        return E_INVALIDARG;

    if (SUCCEEDED(menu_band_create_window(band)))
    {
        SendMessageW(band->toolbar, TB_GETMAXSIZE, 0, (LPARAM)&size);
        if (!size.cy)
        {
            DWORD button_size = SendMessageW(band->toolbar, TB_GETBUTTONSIZE, 0, 0);
            size.cy = HIWORD(button_size);
        }
    }
    TRACE("toolbar size %ldx%ld.\n", size.cx, size.cy);

    if (info->dwMask & DBIM_MINSIZE)
    {
        info->ptMinSize.x = size.cx;
        info->ptMinSize.y = size.cy;
    }
    if (info->dwMask & DBIM_MAXSIZE)
    {
        info->ptMaxSize.x = -1;
        info->ptMaxSize.y = size.cy;
    }
    if (info->dwMask & DBIM_INTEGRAL)
    {
        info->ptIntegral.x = 1;
        info->ptIntegral.y = 1;
    }
    if (info->dwMask & DBIM_ACTUAL)
    {
        info->ptActual.x = size.cx;
        info->ptActual.y = size.cy;
    }
    if (info->dwMask & DBIM_TITLE)
        info->wszTitle[0] = 0;
    if (info->dwMask & DBIM_MODEFLAGS)
        info->dwModeFlags = DBIMF_NORMAL;
    if (info->dwMask & DBIM_BKCOLOR)
        info->dwMask &= ~DBIM_BKCOLOR;
    return S_OK;
}

static const IDeskBandVtbl desk_band_vtbl =
{
    desk_band_QueryInterface,
    desk_band_AddRef,
    desk_band_Release,
    desk_band_GetWindow,
    desk_band_ContextSensitiveHelp,
    desk_band_ShowDW,
    desk_band_CloseDW,
    desk_band_ResizeBorderDW,
    desk_band_GetBandInfo,
};

static HRESULT WINAPI object_with_site_QueryInterface(IObjectWithSite *iface, REFIID iid, void **out)
{
    struct menu_band *band = impl_from_IObjectWithSite(iface);
    return IShellMenu_QueryInterface(&band->IShellMenu_iface, iid, out);
}

static ULONG WINAPI object_with_site_AddRef(IObjectWithSite *iface)
{
    struct menu_band *band = impl_from_IObjectWithSite(iface);
    return IShellMenu_AddRef(&band->IShellMenu_iface);
}

static ULONG WINAPI object_with_site_Release(IObjectWithSite *iface)
{
    struct menu_band *band = impl_from_IObjectWithSite(iface);
    return IShellMenu_Release(&band->IShellMenu_iface);
}

static HRESULT WINAPI object_with_site_SetSite(IObjectWithSite *iface, IUnknown *site)
{
    struct menu_band *band = impl_from_IObjectWithSite(iface);

    TRACE("band %p, site %p.\n", band, site);

    if (site)
        IUnknown_AddRef(site);
    if (band->site)
        IUnknown_Release(band->site);
    band->site = site;

    if (band->window)
    {
        HWND parent = menu_band_get_parent(band);
        if (parent && parent != GetParent(band->window))
            SetParent(band->window, parent);
    }
    return S_OK;
}

static HRESULT WINAPI object_with_site_GetSite(IObjectWithSite *iface, REFIID iid, void **out)
{
    struct menu_band *band = impl_from_IObjectWithSite(iface);

    TRACE("band %p, iid %s, out %p.\n", band, debugstr_guid(iid), out);

    if (!band->site)
    {
        *out = NULL;
        return E_FAIL;
    }
    return IUnknown_QueryInterface(band->site, iid, out);
}

static const IObjectWithSiteVtbl object_with_site_vtbl =
{
    object_with_site_QueryInterface,
    object_with_site_AddRef,
    object_with_site_Release,
    object_with_site_SetSite,
    object_with_site_GetSite,
};

static HRESULT WINAPI menu_band_QueryInterface(IMenuBand *iface, REFIID iid, void **out)
{
    struct menu_band *band = impl_from_IMenuBand(iface);
    return IShellMenu_QueryInterface(&band->IShellMenu_iface, iid, out);
}

static ULONG WINAPI menu_band_AddRef(IMenuBand *iface)
{
    struct menu_band *band = impl_from_IMenuBand(iface);
    return IShellMenu_AddRef(&band->IShellMenu_iface);
}

static ULONG WINAPI menu_band_Release(IMenuBand *iface)
{
    struct menu_band *band = impl_from_IMenuBand(iface);
    return IShellMenu_Release(&band->IShellMenu_iface);
}

static HRESULT WINAPI menu_band_IsMenuMessage(IMenuBand *iface, MSG *msg)
{
    TRACE("iface %p, msg %p.\n", iface, msg);
    return S_FALSE;
}

static HRESULT WINAPI menu_band_TranslateMenuMessage(IMenuBand *iface, MSG *msg, LRESULT *ret)
{
    TRACE("iface %p, msg %p, ret %p.\n", iface, msg, ret);
    return S_FALSE;
}

static const IMenuBandVtbl menu_band_vtbl =
{
    menu_band_QueryInterface,
    menu_band_AddRef,
    menu_band_Release,
    menu_band_IsMenuMessage,
    menu_band_TranslateMenuMessage,
};

static HRESULT WINAPI win_event_handler_QueryInterface(IWinEventHandler *iface, REFIID iid, void **out)
{
    struct menu_band *band = impl_from_IWinEventHandler(iface);
    return IShellMenu_QueryInterface(&band->IShellMenu_iface, iid, out);
}

static ULONG WINAPI win_event_handler_AddRef(IWinEventHandler *iface)
{
    struct menu_band *band = impl_from_IWinEventHandler(iface);
    return IShellMenu_AddRef(&band->IShellMenu_iface);
}

static ULONG WINAPI win_event_handler_Release(IWinEventHandler *iface)
{
    struct menu_band *band = impl_from_IWinEventHandler(iface);
    return IShellMenu_Release(&band->IShellMenu_iface);
}

static HRESULT WINAPI win_event_handler_OnWinEvent(IWinEventHandler *iface, HWND hwnd, UINT msg,
        WPARAM wparam, LPARAM lparam, LRESULT *ret)
{
    TRACE("iface %p, hwnd %p, msg %#x, wparam %#Ix, lparam %#Ix, ret %p.\n", iface, hwnd, msg, wparam, lparam, ret);

    if (ret)
        *ret = 0;
    return S_FALSE;
}

static HRESULT WINAPI win_event_handler_IsWindowOwner(IWinEventHandler *iface, HWND hwnd)
{
    struct menu_band *band = impl_from_IWinEventHandler(iface);

    TRACE("band %p, hwnd %p.\n", band, hwnd);

    return (hwnd && (hwnd == band->window || hwnd == band->toolbar)) ? S_OK : S_FALSE;
}

static const IWinEventHandlerVtbl win_event_handler_vtbl =
{
    win_event_handler_QueryInterface,
    win_event_handler_AddRef,
    win_event_handler_Release,
    win_event_handler_OnWinEvent,
    win_event_handler_IsWindowOwner,
};

static HRESULT WINAPI input_object_QueryInterface(IInputObject *iface, REFIID iid, void **out)
{
    struct menu_band *band = impl_from_IInputObject(iface);
    return IShellMenu_QueryInterface(&band->IShellMenu_iface, iid, out);
}

static ULONG WINAPI input_object_AddRef(IInputObject *iface)
{
    struct menu_band *band = impl_from_IInputObject(iface);
    return IShellMenu_AddRef(&band->IShellMenu_iface);
}

static ULONG WINAPI input_object_Release(IInputObject *iface)
{
    struct menu_band *band = impl_from_IInputObject(iface);
    return IShellMenu_Release(&band->IShellMenu_iface);
}

static HRESULT WINAPI input_object_UIActivateIO(IInputObject *iface, BOOL activate, MSG *msg)
{
    TRACE("iface %p, activate %d, msg %p.\n", iface, activate, msg);
    return S_FALSE;
}

static HRESULT WINAPI input_object_HasFocusIO(IInputObject *iface)
{
    struct menu_band *band = impl_from_IInputObject(iface);
    HWND focus = GetFocus();

    TRACE("band %p.\n", band);

    return (focus && (focus == band->window || focus == band->toolbar)) ? S_OK : S_FALSE;
}

static HRESULT WINAPI input_object_TranslateAcceleratorIO(IInputObject *iface, MSG *msg)
{
    TRACE("iface %p, msg %p.\n", iface, msg);
    return S_FALSE;
}

static const IInputObjectVtbl input_object_vtbl =
{
    input_object_QueryInterface,
    input_object_AddRef,
    input_object_Release,
    input_object_UIActivateIO,
    input_object_HasFocusIO,
    input_object_TranslateAcceleratorIO,
};

HRESULT WINAPI menu_band_create(IUnknown *outer, REFIID iid, void **out)
{
    struct menu_band *band;
    HRESULT hr;

    if (outer)
        return CLASS_E_NOAGGREGATION;

    if (!(band = calloc(1, sizeof(*band))))
        return E_OUTOFMEMORY;

    band->IShellMenu_iface.lpVtbl = &shell_menu_vtbl;
    band->IDeskBand_iface.lpVtbl = &desk_band_vtbl;
    band->IObjectWithSite_iface.lpVtbl = &object_with_site_vtbl;
    band->IMenuBand_iface.lpVtbl = &menu_band_vtbl;
    band->IWinEventHandler_iface.lpVtbl = &win_event_handler_vtbl;
    band->IInputObject_iface.lpVtbl = &input_object_vtbl;
    band->refcount = 1;

    TRACE("Created menu band %p.\n", band);

    hr = IShellMenu_QueryInterface(&band->IShellMenu_iface, iid, out);
    IShellMenu_Release(&band->IShellMenu_iface);
    return hr;
}
