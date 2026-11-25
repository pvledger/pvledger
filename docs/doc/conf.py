"""Shared Sphinx configuration for the English and Russian manuals."""

import os

project = "PVLedger"
author = "Vadim Prishlyak"
copyright = "2026, Vadim Prishlyak"
needs_sphinx = "8.2"

html_theme = "alabaster"
html_theme_options = {"show_relbars": True}
templates_path = ["_templates"]
html_sidebars = {
    "**": ["about.html", "languages.html", "navigation.html", "searchbox.html"]
}
html_copy_source = False


def configure_site_url(app, config):
    site_url = os.environ.get("PVLEDGER_DOCS_URL", "").rstrip("/")
    if site_url:
        config.html_baseurl = f"{site_url}/{config.language}/"


def setup(app):
    app.connect("config-inited", configure_site_url)
