fn main() {
    // Link Objective-C categories from static libwebrtc into each final executable.
    // rustc-link-arg from webrtc-sys does not propagate through its rlib.
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
        println!("cargo:rustc-link-arg=-Wl,-ObjC");
    }
}
