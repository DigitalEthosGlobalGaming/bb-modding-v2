//! Raw mirror of `sdk/include/bbmod.h`. Field order is the ABI: never reorder,
//! only append (and bump `BBM_ABI_MINOR`).
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

pub const BBM_ABI_MAJOR: u16 = 1;
pub const BBM_ABI_MINOR: u16 = 8;

pub type bbm_status = i32;
pub const BBM_OK: i32 = 0;
pub const BBM_ERR: i32 = -1;
pub const BBM_ERR_ARG: i32 = -2;
pub const BBM_ERR_CONFLICT: i32 = -3;
pub const BBM_ERR_UNSUPPORTED: i32 = -4;
pub const BBM_ERR_NOMEM: i32 = -5;
pub const BBM_ERR_STATE: i32 = -6;
pub const BBM_ERR_NOTFOUND: i32 = -7;
pub const BBM_ERR_BUDGET: i32 = -8;

pub type bbm_handle = *mut c_void;
pub type bbm_ctx = c_void;
pub type bbm_id = u64;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct bbm_version {
    pub major: u16,
    pub minor: u16,
    pub patch: u16,
    pub reserved: u16,
}

#[repr(C)]
pub struct bbm_blob {
    pub data: *mut u8,
    pub len: usize,
}

pub const BBM_LOG_DEBUG: i32 = 0;
pub const BBM_LOG_INFO: i32 = 1;
pub const BBM_LOG_WARN: i32 = 2;
pub const BBM_LOG_ERROR: i32 = 3;

pub const BBM_EV_AREA_LOADED: u32 = 1 << 0;
pub const BBM_EV_SAVE_LOADED: u32 = 1 << 1;
pub const BBM_EV_SESSION_JOIN: u32 = 1 << 2;
pub const BBM_EV_SESSION_LEAVE: u32 = 1 << 3;
pub const BBM_EV_PEER_JOIN: u32 = 1 << 4;
pub const BBM_EV_PEER_LEAVE: u32 = 1 << 5;
pub const BBM_EV_OWNER_CHANGED: u32 = 1 << 6;
pub const BBM_EV_PAUSE: u32 = 1 << 7;
pub const BBM_EV_RESUME: u32 = 1 << 8;
pub const BBM_EV_MODSET_MISMATCH: u32 = 1 << 9;
pub const BBM_EV_PAD_CHANGED: u32 = 1 << 10;

#[repr(C)]
pub struct bbm_event {
    pub type_: u32,
    pub reserved: u32,
    pub a: u64,
    pub b: u64,
    pub data: *const c_void,
    pub data_len: usize,
}

pub const BBM_KEY_COUNT: usize = 512;

pub const BBM_PAD_L3: u32 = 0x2;
pub const BBM_PAD_R3: u32 = 0x4;
pub const BBM_PAD_OPTIONS: u32 = 0x8;
pub const BBM_PAD_UP: u32 = 0x10;
pub const BBM_PAD_RIGHT: u32 = 0x20;
pub const BBM_PAD_DOWN: u32 = 0x40;
pub const BBM_PAD_LEFT: u32 = 0x80;
pub const BBM_PAD_L2: u32 = 0x100;
pub const BBM_PAD_R2: u32 = 0x200;
pub const BBM_PAD_L1: u32 = 0x400;
pub const BBM_PAD_R1: u32 = 0x800;
pub const BBM_PAD_TRIANGLE: u32 = 0x1000;
pub const BBM_PAD_CIRCLE: u32 = 0x2000;
pub const BBM_PAD_CROSS: u32 = 0x4000;
pub const BBM_PAD_SQUARE: u32 = 0x8000;
pub const BBM_PAD_TOUCHPAD: u32 = 0x100000;

pub const BBM_REGION_WRITE: u32 = 1 << 0;
pub const BBM_REGION_EXEC: u32 = 1 << 1;
pub const BBM_REGION_IMAGE: u32 = 1 << 2;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct bbm_region {
    pub base: usize,
    pub size: usize,
    pub flags: u32,
    pub reserved: u32,
}

pub const BBM_NET_UNRELIABLE: u32 = 0;
pub const BBM_NET_RELIABLE: u32 = 1 << 0;
pub const BBM_NET_ORDERED: u32 = 1 << 1;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct bbm_session_info {
    pub in_session: u32,
    pub is_owner: u32,
    pub dev_session: u32,
    pub peer_count: u32,
    pub local_peer: u64,
    pub owner_peer: u64,
    pub modset_hash: u64,
}

pub type bbm_net_cb = unsafe extern "C" fn(*mut c_void, u64, *const c_void, usize);
pub type bbm_frame_cb = unsafe extern "C" fn(*mut c_void);
pub type bbm_panel_cb = unsafe extern "C" fn(*mut c_void);
pub type bbm_thread_fn = unsafe extern "C" fn(*mut c_void, *const i32);

type H = bbm_handle;
type S = bbm_status;
type Cs = *const c_char;

#[repr(C)]
pub struct bbm_host {
    pub size: u32,
    pub abi_major: u16,
    pub abi_minor: u16,

    pub log: unsafe extern "C" fn(H, i32, Cs),
    pub last_error: unsafe extern "C" fn(H) -> Cs,
    pub alloc: unsafe extern "C" fn(usize) -> *mut c_void,
    pub free: unsafe extern "C" fn(*mut c_void),
    pub config_get: unsafe extern "C" fn(H, Cs, *mut c_char, usize) -> S,
    pub mod_info: unsafe extern "C" fn(H, *mut Cs, *mut bbm_version),
    pub inflight: unsafe extern "C" fn(H) -> *mut i32,
    pub release: unsafe extern "C" fn(H, bbm_id) -> S,

    pub eboot_base: unsafe extern "C" fn(H) -> usize,
    pub ps4_to_host: unsafe extern "C" fn(H, u64) -> usize,
    pub read: unsafe extern "C" fn(H, usize, *mut c_void, usize) -> S,
    pub write: unsafe extern "C" fn(H, usize, *const c_void, usize) -> S,
    pub scan: unsafe extern "C" fn(H, Cs, usize, usize, *mut usize) -> S,

    pub patch_bytes: unsafe extern "C" fn(H, usize, *const c_void, usize, *mut bbm_id) -> S,
    pub patch_xml: unsafe extern "C" fn(H, Cs, Cs, *mut bbm_id) -> S,

    pub hook_inline: unsafe extern "C" fn(H, usize, *mut c_void, *mut *mut c_void, *mut bbm_id) -> S,
    pub hook_slot:
        unsafe extern "C" fn(H, *mut *mut c_void, *mut c_void, *mut *mut c_void, *mut bbm_id) -> S,

    pub on_frame: unsafe extern "C" fn(H, bbm_frame_cb, *mut c_void, *mut bbm_id) -> S,
    pub thread_spawn: unsafe extern "C" fn(H, bbm_thread_fn, *mut c_void, *mut bbm_id) -> S,
    pub subscribe: unsafe extern "C" fn(H, u32) -> S,

    pub ui_panel: unsafe extern "C" fn(H, Cs, bbm_panel_cb, *mut c_void, *mut bbm_id) -> S,
    pub ui_notify: unsafe extern "C" fn(H, Cs),
    pub ui_text: unsafe extern "C" fn(H, Cs),
    pub ui_button: unsafe extern "C" fn(H, Cs) -> i32,
    pub ui_checkbox: unsafe extern "C" fn(H, Cs, *mut i32) -> i32,
    pub ui_slider_float: unsafe extern "C" fn(H, Cs, *mut f32, f32, f32) -> i32,
    pub ui_separator: unsafe extern "C" fn(H),
    pub ui_same_line: unsafe extern "C" fn(H),
    pub ui_imgui_context: unsafe extern "C" fn(H) -> *mut c_void,

    pub file_override: unsafe extern "C" fn(H, Cs, Cs, *mut bbm_id) -> S,

    pub net_channel_open: unsafe extern "C" fn(H, Cs, u32, *mut bbm_id) -> S,
    pub net_on_message: unsafe extern "C" fn(H, bbm_id, bbm_net_cb, *mut c_void) -> S,
    pub net_send: unsafe extern "C" fn(H, bbm_id, u64, *const c_void, usize) -> S,
    pub net_broadcast: unsafe extern "C" fn(H, bbm_id, *const c_void, usize) -> S,
    pub session_info: unsafe extern "C" fn(H, *mut bbm_session_info) -> S,

    // ---- ABI 1.1 (check `size` before use; see `bbm_host::has_v1_1`) ----
    pub key_down: unsafe extern "C" fn(H, i32) -> i32,
    pub key_pressed: unsafe extern "C" fn(H, i32) -> i32,
    pub pad_buttons: unsafe extern "C" fn(H) -> u32,
    pub pad_pressed: unsafe extern "C" fn(H) -> u32,
    pub ui_slider_int: unsafe extern "C" fn(H, Cs, *mut i32, i32, i32) -> i32,
    pub ui_input_text: unsafe extern "C" fn(H, Cs, *mut c_char, usize) -> i32,
    pub ui_collapsing_header: unsafe extern "C" fn(H, Cs) -> i32,
    pub ui_combo: unsafe extern "C" fn(H, Cs, *mut i32, Cs) -> i32,
    pub ui_text_colored: unsafe extern "C" fn(H, f32, f32, f32, f32, Cs),

    // ---- ABI 1.2 ----
    pub eboot_size: unsafe extern "C" fn(H) -> usize,
    pub mem_region: unsafe extern "C" fn(H, usize, *mut bbm_region) -> S,
    pub pad_info: unsafe extern "C" fn(H, *mut bbm_pad_info) -> S,
    pub pad_trigger: unsafe extern "C" fn(H, u32, *const u8) -> S,
    pub pad_release: unsafe extern "C" fn(H, u32) -> S,
    pub pad_lightbar: unsafe extern "C" fn(H, u8, u8, u8) -> S,
    pub player_snapshot: unsafe extern "C" fn(H, *mut bbm_player_snapshot) -> S,
    pub player_adjust: unsafe extern "C" fn(H, u32, u64, i32, i32, *mut i32) -> S,
    pub player_modifier: unsafe extern "C" fn(H, u32, u32, i32, *mut bbm_id) -> S,
    pub ui_contribute: unsafe extern "C" fn(H, Cs, Cs, i32, bbm_panel_cb, *mut c_void, *mut bbm_id) -> S,
    pub equipment_snapshot: unsafe extern "C" fn(H, *mut bbm_equipment_snapshot) -> S,
    pub inventory_page: unsafe extern "C" fn(H, u64, u32, u32, *mut bbm_inventory_page) -> S,
    pub item_give: unsafe extern "C" fn(H, u32, u32, u64, *mut u64) -> S,
    pub item_give_status: unsafe extern "C" fn(H, u64, *mut bbm_item_give_result) -> S,
    pub pad_haptics: unsafe extern "C" fn(H, *const f32, u32) -> S,
    pub characters_page: unsafe extern "C" fn(H, u64, u32, u32, *mut bbm_character_page) -> S,
    pub ui_table_begin: unsafe extern "C" fn(H,Cs,*const Cs,u32,f32,*mut i32,*mut i32)->i32,
    pub ui_table_next_row: unsafe extern "C" fn(H),
    pub ui_table_next_column: unsafe extern "C" fn(H),
    pub ui_table_end: unsafe extern "C" fn(H),
    pub menu_register: unsafe extern "C" fn(H,*const bbm_menu_node,*mut bbm_id)->S,
    pub menu_update: unsafe extern "C" fn(H,bbm_id,*const bbm_menu_node)->S,
    pub menu_state: unsafe extern "C" fn(H,*mut bbm_menu_state)->S,
}

pub const BBM_EV_NATIVE_MENU:u32=1<<20;
#[repr(C)]
#[derive(Clone,Copy)]
pub struct bbm_menu_node {
    pub size:u32,pub kind:u32,pub flags:u32,pub order:i32,pub value:i32,
    pub key:[c_char;64],pub parent:[c_char;192],pub label:[c_char;128],pub help:[c_char;256],pub target:[c_char;192],
}
#[repr(C)]
#[derive(Clone,Copy)]
pub struct bbm_menu_state {
    pub size:u32,pub capabilities:u32,pub revision:u64,pub generation:u64,pub screen:[c_char;192],
}
#[repr(C)]
#[derive(Clone,Copy)]
pub struct bbm_menu_event {
    pub size:u32,pub kind:u32,pub generation:u64,pub revision:u64,pub node:u64,pub value:i32,pub reserved:u32,
    pub control:u64,pub previous_value:i32,pub flags:u32,
    pub owner:[c_char;128],pub key:[c_char;64],pub screen:[c_char;192],
}
impl Default for bbm_menu_node {fn default()->Self{unsafe{core::mem::zeroed()}}}
impl Default for bbm_menu_state {fn default()->Self{unsafe{core::mem::zeroed()}}}
impl Default for bbm_menu_event {fn default()->Self{unsafe{core::mem::zeroed()}}}

/// Byte offset just past a field of `bbm_host` (`offset_of!` needs Rust 1.77).
macro_rules! field_end {
    ($field:ident) => {{
        let u = core::mem::MaybeUninit::<bbm_host>::uninit();
        let base = u.as_ptr() as usize;
        // SAFETY: only the field's address is taken; nothing is read.
        let f = unsafe { core::ptr::addr_of!((*u.as_ptr()).$field) } as usize;
        f - base + core::mem::size_of::<usize>()
    }};
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct bbm_pad_info {
    pub kind: u32, pub connection: u32, pub features: u32,
    pub l2_depth: u8, pub r2_depth: u8, pub reserved: u16,
    pub device_id: u64,
}
pub const BBM_EV_PLAYER_CHANGED: u32 = 1 << 11;
pub const BBM_PLAYER_AVAILABLE: u32 = 1;
pub const BBM_RESOURCE_HEALTH: u32 = 1;
pub const BBM_RESOURCE_STAMINA: u32 = 2;
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct bbm_player_snapshot {
    pub size: u32, pub flags: u32, pub generation: u64,
    pub actor: usize, pub component: usize,
    pub health: i32, pub max_health: i32, pub stamina: i32, pub max_stamina: i32,
}
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct bbm_player_change { pub previous: bbm_player_snapshot, pub current: bbm_player_snapshot }
pub const BBM_EV_EQUIPMENT_CHANGED: u32 = 1 << 15;
pub const BBM_EV_ATTACK_OBSERVED: u32 = 1 << 18;
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct bbm_attack_observed {
    pub size: u32, pub source: u32, pub generation: u64,
    pub actor: usize, pub behavior_id: i32, pub reserved: u32,
}
pub const BBM_EV_ITEM_USE_OBSERVED: u32 = 1 << 16;
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct bbm_item_use {
    pub size: u32, pub success: u32, pub generation: u64,
    pub actor: usize, pub item_id: i32, pub reserved: u32,
}
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct bbm_equipment_snapshot {
    pub size: u32, pub flags: u32, pub generation: u64,
    pub actor: usize, pub character: usize,
    pub mode: u32, pub selections: [u32;6], pub runtime_references: [u32;16],
    pub saved_item_ids: [u32;16], pub selected_even: u32, pub selected_odd: u32,
    pub reserved: u32,
}
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct bbm_equipment_change { pub previous: bbm_equipment_snapshot, pub current: bbm_equipment_snapshot }
pub const BBM_EV_INVENTORY_CHANGED: u32 = 1 << 17;
pub const BBM_INVENTORY_PAGE_SIZE: usize = 64;
pub const BBM_INVENTORY_MAX_ITEMS: usize = 4096;
pub const BBM_INVENTORY_AVAILABLE: u32 = 1;
pub const BBM_INVENTORY_ID_RESOLVED: u32 = 1;
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct bbm_inventory_entry {
    pub slot: u32, pub reference: u32, pub encoded_item_id: u32, pub quantity: u32,
    pub metadata_04: u32, pub metadata_0c: u32, pub flags: u32, pub reserved: u32,
}
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct bbm_inventory_summary {
    pub size: u32, pub flags: u32, pub generation: u64, pub revision: u64,
    pub actor: usize, pub character: usize, pub slot_count: u32, pub item_count: u32,
}
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct bbm_inventory_page {
    pub snapshot: bbm_inventory_summary, pub offset: u32, pub count: u32,
    pub entries: [bbm_inventory_entry; BBM_INVENTORY_PAGE_SIZE],
}
impl Default for bbm_inventory_page {
    fn default() -> Self { Self { snapshot:Default::default(),offset:0,count:0,entries:[bbm_inventory_entry::default();BBM_INVENTORY_PAGE_SIZE] } }
}
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct bbm_inventory_change { pub previous: bbm_inventory_summary, pub current: bbm_inventory_summary }
pub const BBM_ITEM_PENDING:u32=1;
pub const BBM_ITEM_RUNNING:u32=2;
pub const BBM_ITEM_SUBMITTED:u32=3;
pub const BBM_ITEM_CANCELLED:u32=4;
pub const BBM_ITEM_FAILED:u32=5;
#[repr(C)]
#[derive(Debug,Clone,Copy,Default)]
pub struct bbm_item_give_result {
    pub size:u32,pub state:u32,pub request_id:u64,pub generation:u64,
    pub encoded_item_id:u32,pub quantity:u32,
}
impl bbm_host {
    pub fn has_characters(&self)->bool {self.size as usize>=field_end!(characters_page)}
    pub fn has_native_menus(&self)->bool {self.size as usize>=field_end!(menu_state)}
    pub fn has_item_actions(&self)->bool {self.size as usize>=field_end!(item_give_status)}
    pub fn has_inventory(&self) -> bool { self.size as usize >= field_end!(inventory_page) }
    pub fn has_equipment(&self) -> bool { self.size as usize >= field_end!(equipment_snapshot) }
    pub fn has_ui_slots(&self) -> bool { self.size as usize >= field_end!(ui_contribute) }
    pub fn has_gameplay(&self) -> bool { self.size as usize >= field_end!(player_modifier) }
    pub fn has_v1_3(&self) -> bool { self.size as usize >= field_end!(pad_lightbar) }
    /// The host's table includes the ABI 1.1 fields (input, widgets).
    pub fn has_v1_1(&self) -> bool { self.size as usize >= field_end!(ui_text_colored) }
    /// The host's table includes the ABI 1.2 fields (eboot_size, mem_region).
    pub fn has_v1_2(&self) -> bool { self.size as usize >= field_end!(mem_region) }
}

#[repr(C)]
pub struct bbm_mod_api {
    pub size: u32,
    pub abi_major: u16,
    pub abi_minor: u16,
    pub on_load: Option<unsafe extern "C" fn(*mut bbm_ctx) -> S>,
    pub on_unload: Option<unsafe extern "C" fn(*mut bbm_ctx)>,
    pub on_save_state: Option<unsafe extern "C" fn(*mut bbm_ctx, *mut bbm_blob) -> S>,
    pub on_restore_state:
        Option<unsafe extern "C" fn(*mut bbm_ctx, *const bbm_blob, bbm_version) -> S>,
    pub on_event: Option<unsafe extern "C" fn(*mut bbm_ctx, *const bbm_event)>,
    pub destroy: Option<unsafe extern "C" fn(*mut bbm_ctx)>,
}

pub type bbm_entry_fn =
    unsafe extern "C" fn(*const bbm_host, bbm_handle, *mut bbm_mod_api, *mut *mut bbm_ctx) -> S;

// ABI 1.6: sampled loaded characters, with no native AI mutation.
pub const BBM_EV_CHARACTERS_CHANGED:u32=1<<19;
#[repr(C)]
#[derive(Debug,Clone,Copy,Default,PartialEq,Eq)]
pub struct bbm_character {
 pub size:u32,pub flags:u32,pub token:u64,pub handle:u32,pub kind:u32,
 pub health:i32,pub max_health:i32,pub team_type:i32,pub npc_param:i32,pub name_id:i32,pub script_value:i32,
 pub backread_state:i32,pub update_interval:i32,
}
#[repr(C)]
#[derive(Debug,Clone,Copy,Default,PartialEq,Eq)]
pub struct bbm_character_summary {pub size:u32,pub flags:u32,pub revision:u64,pub epoch:u64,pub count:u32,pub reserved:u32}
#[repr(C)]
#[derive(Debug,Clone,Copy,Default)]
pub struct bbm_character_page {pub snapshot:bbm_character_summary,pub offset:u32,pub count:u32,pub entries:[bbm_character;32]}
