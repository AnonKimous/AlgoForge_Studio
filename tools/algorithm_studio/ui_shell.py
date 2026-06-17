from __future__ import annotations

from pathlib import Path

import tkinter as tk
from tkinter import ttk

try:
    from .shared import COLORS, SIDEBAR_EXPANDED_WIDTH, _resolve_codex_command
except ImportError:
    from shared import COLORS, SIDEBAR_EXPANDED_WIDTH, _resolve_codex_command


def configure_style(app: object) -> None:
    style = ttk.Style(app.root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass

    style.configure("TFrame", background=COLORS["window"])
    style.configure("TLabel", background=COLORS["window"], foreground=COLORS["text"])
    style.configure("TButton", padding=(10, 6))
    style.configure("TLabelframe", background=COLORS["panel"], foreground=COLORS["text"])
    style.configure("TLabelframe.Label", background=COLORS["panel"], foreground=COLORS["accent"])
    style.configure("TNotebook", background=COLORS["window"], borderwidth=0)
    style.configure("TNotebook.Tab", padding=(12, 8))
    style.map("TNotebook.Tab", foreground=[("selected", COLORS["accent"]), ("!selected", COLORS["text"])])
    style.configure("Dark.Horizontal.TScale", background=COLORS["panel"])
    app.root.option_add("*Font", ("Segoe UI", 10))


def build_ui(app: object) -> None:
    app.root.columnconfigure(0, weight=1)
    app.root.rowconfigure(1, weight=1)
    build_toolbar(app)
    build_main_area(app)


def build_toolbar(app: object) -> None:
    toolbar = ttk.Frame(app.root, padding=(12, 10))
    toolbar.grid(row=0, column=0, sticky="ew")
    buttons = [
        ("New", app._new_project),
        ("Load Package", app._load_package),
        ("Save Package", app._save_package),
        ("Build", app._build_current_algorithm),
    ]
    for index, (label, command) in enumerate(buttons):
        button = ttk.Button(toolbar, text=label, command=command)
        button.grid(row=0, column=index, padx=(0, 8))
        if label == "Build":
            app.build_button = button


def build_main_area(app: object) -> None:
    main = ttk.Frame(app.root, padding=(12, 0, 12, 12))
    main.grid(row=1, column=0, sticky="nsew")
    main.columnconfigure(0, weight=0)
    main.columnconfigure(1, weight=1)
    main.columnconfigure(2, weight=0, minsize=SIDEBAR_EXPANDED_WIDTH)
    main.rowconfigure(0, weight=1)

    app._build_palette_panel(main)
    app._build_canvas_panel(main)
    app._build_sidebar_panel(main)


def build_welcome_page(app: object) -> None:
    overlay = ttk.Frame(app.root, padding=24)
    overlay.place(relx=0, rely=0, relwidth=1, relheight=1)
    overlay.lift()
    overlay.columnconfigure(0, weight=1)
    overlay.rowconfigure(0, weight=1)
    app.welcome_frame = overlay

    shell = ttk.Frame(overlay, padding=24)
    shell.place(relx=0.5, rely=0.5, anchor="center")
    shell.columnconfigure(0, weight=1)

    card = ttk.LabelFrame(shell, text="Welcome", padding=20)
    card.grid(row=0, column=0, sticky="nsew")
    card.columnconfigure(0, weight=1)

    ttk.Label(
        card,
        text="Choose one way to enter: connect to a local Codex agent or wire in your API once.",
        wraplength=440,
        foreground=COLORS["text"],
    ).grid(row=0, column=0, sticky="ew", pady=(0, 12))

    codex_binary = _resolve_codex_command(app.agent_command_var.get())
    codex_label = "Local Codex binary detected" if Path(codex_binary).exists() else "Local Codex binary not found"
    ttk.Label(card, text=codex_label, foreground=COLORS["muted"]).grid(row=1, column=0, sticky="ew", pady=(0, 8))

    buttons = ttk.Frame(card)
    buttons.grid(row=2, column=0, sticky="ew", pady=(8, 8))
    buttons.columnconfigure((0, 1), weight=1)

    api_button = ttk.Button(buttons, text="Connect API", command=app._welcome_connect_api)
    api_button.grid(row=0, column=0, sticky="ew", padx=(0, 8))
    codex_button = ttk.Button(buttons, text="Use Existing Agent", command=app._welcome_use_codex)
    codex_button.grid(row=0, column=1, sticky="ew")

    ttk.Label(card, textvariable=app.welcome_status_var, wraplength=440, foreground=COLORS["accent"]).grid(
        row=3, column=0, sticky="ew", pady=(8, 0)
    )


def destroy_welcome_page(app: object) -> None:
    if app.welcome_frame is not None:
        app.welcome_frame.destroy()
        app.welcome_frame = None


def finalize_welcome(app: object, provider: str) -> None:
    app.provider_var.set(provider)
    app.agent_ready_var.set(True)
    app._sync_agent_client_settings()
    destroy_welcome_page(app)
    app._log(f"Connected agent source: {provider}.")


def welcome_use_codex(app: object) -> None:
    app.welcome_status_var.set("Using local Codex binary.")
    finalize_welcome(app, "codex")


def welcome_connect_api(app: object) -> None:
    app.welcome_status_var.set("API connection is configured from the sidebar.")
    finalize_welcome(app, "api")


def welcome_import_existing_agent(app: object) -> None:
    welcome_use_codex(app)
