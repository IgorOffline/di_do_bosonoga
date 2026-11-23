use std::process::Command;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=RUSTC");
    let compiler = std::env::var_os("RUSTC").ok_or("Cargo did not set RUSTC")?;
    let output = Command::new(compiler).arg("--version").output()?;
    if !output.status.success() {
        return Err("could not read the Rust compiler version".into());
    }
    let version = String::from_utf8(output.stdout)?;
    println!("cargo:rustc-env=BOSONOGA_RUSTC_VERSION={}", version.trim());
    Ok(())
}
