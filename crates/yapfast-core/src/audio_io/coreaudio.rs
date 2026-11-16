//! AudioQueue streams with bounded buffers and blocking reads/writes off the UI thread.
use super::Format;
use anyhow::{Result, bail};
use std::{
    ffi::{CString, c_int, c_void},
    ptr::NonNull,
    time::Duration,
};

#[cfg_attr(not(feature = "discord"), allow(dead_code))]
pub fn raise_priority(_: super::Priority) -> Result<&'static str, String> {
    Err("CoreAudio schedules its callbacks; worker priority is unchanged".into())
}

#[cfg(feature = "discord")]
pub fn full_speed(_: bool) {}

unsafe extern "C" {
    fn yf_audio_open(
        input: c_int,
        device: *const i8,
        rate: u32,
        channels: u32,
        floating: c_int,
        frames: u32,
        error: *mut c_int,
    ) -> *mut c_void;
    fn yf_audio_close(stream: *mut c_void);
    fn yf_audio_write(stream: *mut c_void, data: *const u8, length: usize) -> c_int;
    fn yf_audio_read(stream: *mut c_void, data: *mut u8, length: usize) -> c_int;
    fn yf_audio_drain(stream: *mut c_void) -> c_int;
    fn yf_audio_flush(stream: *mut c_void);
    fn yf_audio_latency(stream: *mut c_void) -> u32;
}
struct Stream(NonNull<c_void>);
// AudioQueue handles its callback thread; all shared state is protected in C.
unsafe impl Send for Stream {}
impl Drop for Stream {
    fn drop(&mut self) {
        unsafe { yf_audio_close(self.0.as_ptr()) }
    }
}
fn check(status: c_int) -> Result<()> {
    if status != 0 {
        bail!("CoreAudio error {status}");
    }
    Ok(())
}
impl Stream {
    fn open(input: bool, device: Option<&str>, format: Format, duration: Duration) -> Result<Self> {
        if format.rate == 0 || format.channels == 0 {
            bail!("Invalid audio format");
        }
        let device = device.map(CString::new).transpose()?;
        let frames = (format.bytes(duration.min(Duration::from_secs(1))) / format.frame_bytes())
            .max(64) as u32;
        let mut error = 0;
        let pointer = unsafe {
            yf_audio_open(
                input.into(),
                device.as_ref().map_or(std::ptr::null(), |d| d.as_ptr()),
                format.rate,
                format.channels.into(),
                format.float.into(),
                frames,
                &mut error,
            )
        };
        check(error)?;
        Ok(Self(NonNull::new(pointer).ok_or_else(|| {
            anyhow::anyhow!("CoreAudio allocation failed")
        })?))
    }
}
pub struct Output(Stream);
impl Output {
    pub fn open(
        device: Option<&str>,
        _: &'static str,
        format: Format,
        queue: Option<Duration>,
    ) -> Result<Self> {
        Stream::open(
            false,
            device,
            format,
            queue.unwrap_or(Duration::from_millis(60)) / 3,
        )
        .map(Self)
    }
    pub fn write(&mut self, data: &[u8]) -> Result<()> {
        check(unsafe { yf_audio_write(self.0.0.as_ptr(), data.as_ptr(), data.len()) })
    }
    pub fn drain(&mut self) -> Result<()> {
        check(unsafe { yf_audio_drain(self.0.0.as_ptr()) })
    }
    pub fn flush(&mut self) {
        unsafe { yf_audio_flush(self.0.0.as_ptr()) }
    }
    pub fn latency(&self) -> Duration {
        Duration::from_micros(unsafe { yf_audio_latency(self.0.0.as_ptr()) }.into())
    }
}
pub struct Input(Stream);
impl Input {
    pub fn open(
        device: Option<&str>,
        _: &'static str,
        format: Format,
        fragment: Duration,
    ) -> Result<Self> {
        Stream::open(true, device, format, fragment).map(Self)
    }
    pub fn read(&mut self, data: &mut [u8]) -> Result<()> {
        check(unsafe { yf_audio_read(self.0.0.as_ptr(), data.as_mut_ptr(), data.len()) })
    }
}
#[cfg(feature = "discord")]
pub fn endpoint(device: Option<&str>, _: bool) -> String {
    device.unwrap_or_default().to_owned()
}
#[cfg(feature = "discord")]
pub fn others(
    _: fn(&str) -> bool,
    _: super::Hear,
    _: std::sync::Arc<std::sync::atomic::AtomicBool>,
) -> Result<std::thread::JoinHandle<()>> {
    bail!("Le partage du son des applications n’est pas encore disponible sur macOS.")
}
