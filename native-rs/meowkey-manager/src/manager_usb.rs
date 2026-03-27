use anyhow::{Result, anyhow};

#[cfg(target_os = "linux")]
use std::ptr;

#[cfg(target_os = "linux")]
const DEFAULT_VENDOR_ID: u16 = 0xcafe;
#[cfg(target_os = "linux")]
const DEFAULT_PRODUCT_ID: u16 = 0x4005;
#[cfg(target_os = "linux")]
const MANAGER_INTERFACE_NUMBER: i32 = 1;
#[cfg(target_os = "linux")]
const BULK_OUT_ENDPOINT: u8 = 0x03;
#[cfg(target_os = "linux")]
const BULK_IN_ENDPOINT: u8 = 0x83;
#[cfg(target_os = "linux")]
const TRANSFER_TIMEOUT_MS: u32 = 2000;

pub struct ManagerUsbDevice {
    #[cfg(target_os = "linux")]
    context: *mut ffi::libusb_context,
    #[cfg(target_os = "linux")]
    handle: *mut ffi::libusb_device_handle,
    #[cfg(target_os = "linux")]
    interface_number: i32,
}

impl ManagerUsbDevice {
    #[cfg(target_os = "linux")]
    pub fn open() -> Result<Self> {
        let mut context = ptr::null_mut();
        check_libusb(unsafe { ffi::libusb_init(&mut context) }, "初始化 libusb 失败")?;

        let mut device_list = ptr::null_mut();
        let count = unsafe { ffi::libusb_get_device_list(context, &mut device_list) };
        if count < 0 {
            unsafe {
                ffi::libusb_exit(context);
            }
            bail!("枚举 USB 设备失败: {count}");
        }

        let device_slice = unsafe { std::slice::from_raw_parts(device_list, count as usize) };
        let mut opened = None;
        for &device in device_slice {
            if device.is_null() {
                continue;
            }

            let mut descriptor = ffi::libusb_device_descriptor::default();
            if unsafe { ffi::libusb_get_device_descriptor(device, &mut descriptor) } != 0 {
                continue;
            }

            if descriptor.idVendor != DEFAULT_VENDOR_ID || descriptor.idProduct != DEFAULT_PRODUCT_ID {
                continue;
            }

            let mut handle = ptr::null_mut();
            if unsafe { ffi::libusb_open(device, &mut handle) } != 0 || handle.is_null() {
                continue;
            }

            unsafe {
                let active = ffi::libusb_kernel_driver_active(handle, MANAGER_INTERFACE_NUMBER);
                if active == 1 {
                    let _ = ffi::libusb_detach_kernel_driver(handle, MANAGER_INTERFACE_NUMBER);
                }
            }

            if unsafe { ffi::libusb_claim_interface(handle, MANAGER_INTERFACE_NUMBER) } != 0 {
                unsafe {
                    ffi::libusb_close(handle);
                }
                continue;
            }

            opened = Some(Self {
                context,
                handle,
                interface_number: MANAGER_INTERFACE_NUMBER,
            });
            break;
        }

        unsafe {
            ffi::libusb_free_device_list(device_list, 1);
        }

        if let Some(device) = opened {
            return Ok(device);
        }

        unsafe {
            ffi::libusb_exit(context);
        }
        bail!("没有找到可打开的 MeowKey 正式管理接口。")
    }

    #[cfg(not(target_os = "linux"))]
    pub fn open() -> Result<Self> {
        Err(anyhow!("正式管理 bulk 通道后端当前只在 Linux 上实现。"))
    }

    #[cfg(target_os = "linux")]
    pub fn send_command(&self, request: &[u8]) -> Result<Vec<u8>> {
        let mut transferred = 0i32;
        check_libusb(
            unsafe {
                ffi::libusb_bulk_transfer(
                    self.handle,
                    BULK_OUT_ENDPOINT,
                    request.as_ptr() as *mut u8,
                    request.len() as i32,
                    &mut transferred,
                    TRANSFER_TIMEOUT_MS,
                )
            },
            "写入管理请求失败",
        )?;
        if transferred as usize != request.len() {
            bail!("管理请求写入不完整: {transferred}/{}", request.len());
        }

        let mut response = vec![0u8; 1024];
        let mut scratch = [0u8; 256];
        let mut total = 0usize;
        let mut expected = 10usize;

        while total < expected {
            let mut read = 0i32;
            check_libusb(
                unsafe {
                    ffi::libusb_bulk_transfer(
                        self.handle,
                        BULK_IN_ENDPOINT,
                        scratch.as_mut_ptr(),
                        scratch.len() as i32,
                        &mut read,
                        TRANSFER_TIMEOUT_MS,
                    )
                },
                "读取管理响应失败",
            )?;
            if read <= 0 {
                bail!("管理响应为空。");
            }

            let read = read as usize;
            if total + read > response.len() {
                bail!("管理响应超过固定接收缓冲区。");
            }

            response[total..(total + read)].copy_from_slice(&scratch[..read]);
            total += read;

            if total >= 10 {
                let payload_length = u16::from_le_bytes([response[8], response[9]]) as usize;
                expected = 10 + payload_length;
                if expected > response.len() {
                    bail!("管理响应长度异常: {expected}");
                }
            }
        }

        response.truncate(total);
        Ok(response)
    }

    #[cfg(not(target_os = "linux"))]
    pub fn send_command(&self, _request: &[u8]) -> Result<Vec<u8>> {
        Err(anyhow!("正式管理 bulk 通道后端当前只在 Linux 上实现。"))
    }
}

impl Drop for ManagerUsbDevice {
    fn drop(&mut self) {
        #[cfg(target_os = "linux")]
        unsafe {
            if !self.handle.is_null() {
                let _ = ffi::libusb_release_interface(self.handle, self.interface_number);
                ffi::libusb_close(self.handle);
            }
            if !self.context.is_null() {
                ffi::libusb_exit(self.context);
            }
        }
    }
}

#[cfg(target_os = "linux")]
fn check_libusb(code: i32, context: &str) -> Result<()> {
    if code == 0 {
        return Ok(());
    }

    Err(anyhow!("{context}: {}", libusb_error_name(code)))
}

#[cfg(target_os = "linux")]
fn libusb_error_name(code: i32) -> &'static str {
    match code {
        -1 => "IO",
        -2 => "INVALID_PARAM",
        -3 => "ACCESS",
        -4 => "NO_DEVICE",
        -5 => "NOT_FOUND",
        -6 => "BUSY",
        -7 => "TIMEOUT",
        -8 => "OVERFLOW",
        -9 => "PIPE",
        -10 => "INTERRUPTED",
        -11 => "NO_MEM",
        -12 => "NOT_SUPPORTED",
        _ => "UNKNOWN",
    }
}

#[cfg(target_os = "linux")]
mod ffi {
    use std::os::raw::{c_int, c_uchar, c_uint, c_void};

    #[repr(C)]
    pub struct libusb_context(c_void);

    #[repr(C)]
    pub struct libusb_device(c_void);

    #[repr(C)]
    pub struct libusb_device_handle(c_void);

    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    pub struct libusb_device_descriptor {
        pub bLength: u8,
        pub bDescriptorType: u8,
        pub bcdUSB: u16,
        pub bDeviceClass: u8,
        pub bDeviceSubClass: u8,
        pub bDeviceProtocol: u8,
        pub bMaxPacketSize0: u8,
        pub idVendor: u16,
        pub idProduct: u16,
        pub bcdDevice: u16,
        pub iManufacturer: u8,
        pub iProduct: u8,
        pub iSerialNumber: u8,
        pub bNumConfigurations: u8,
    }

    #[link(name = "usb-1.0")]
    unsafe extern "C" {
        pub fn libusb_init(ctx: *mut *mut libusb_context) -> c_int;
        pub fn libusb_exit(ctx: *mut libusb_context);
        pub fn libusb_get_device_list(
            ctx: *mut libusb_context,
            list: *mut *mut *mut libusb_device,
        ) -> isize;
        pub fn libusb_free_device_list(list: *mut *mut libusb_device, unref_devices: c_int);
        pub fn libusb_get_device_descriptor(
            dev: *mut libusb_device,
            desc: *mut libusb_device_descriptor,
        ) -> c_int;
        pub fn libusb_open(
            dev: *mut libusb_device,
            handle: *mut *mut libusb_device_handle,
        ) -> c_int;
        pub fn libusb_close(handle: *mut libusb_device_handle);
        pub fn libusb_claim_interface(handle: *mut libusb_device_handle, interface_number: c_int)
            -> c_int;
        pub fn libusb_release_interface(
            handle: *mut libusb_device_handle,
            interface_number: c_int,
        ) -> c_int;
        pub fn libusb_kernel_driver_active(
            handle: *mut libusb_device_handle,
            interface_number: c_int,
        ) -> c_int;
        pub fn libusb_detach_kernel_driver(
            handle: *mut libusb_device_handle,
            interface_number: c_int,
        ) -> c_int;
        pub fn libusb_bulk_transfer(
            handle: *mut libusb_device_handle,
            endpoint: c_uchar,
            data: *mut c_uchar,
            length: c_int,
            transferred: *mut c_int,
            timeout: c_uint,
        ) -> c_int;
    }
}
