mod app;
mod cbor;
mod models;
mod transport;

use eframe::egui;

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1440.0, 940.0])
            .with_min_inner_size([1100.0, 760.0])
            .with_title("MeowKey Manager"),
        ..Default::default()
    };

    eframe::run_native(
        "MeowKey Manager",
        options,
        Box::new(|cc| Ok(Box::new(app::MeowKeyManagerApp::new(cc)))),
    )
}
