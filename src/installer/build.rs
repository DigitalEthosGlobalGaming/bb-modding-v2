//! Embeds `payload/payload.zip` (staged by `payload/build-payload.ps1` or CI). Without it the
//! installer still builds, reports that it has no payload, and refuses to install.
use std::{env, fs, path::PathBuf};

fn main() {
    println!("cargo:rerun-if-changed=payload/payload.zip");
    let out = PathBuf::from(env::var("OUT_DIR").unwrap()).join("payload.zip");
    match fs::copy("payload/payload.zip", &out) {
        Ok(_) => println!("cargo:rustc-env=BBINSTALL_HAS_PAYLOAD=1"),
        Err(_) => {
            // An empty ZIP archive: just the end-of-central-directory record.
            let mut eocd = vec![0x50, 0x4b, 0x05, 0x06];
            eocd.extend([0u8; 18]);
            fs::write(&out, eocd).unwrap();
            println!("cargo:rustc-env=BBINSTALL_HAS_PAYLOAD=0");
        }
    }
}
