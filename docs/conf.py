# SPDX-FileCopyrightText: 2026 I2C microSD Expansion contributors
#
# SPDX-License-Identifier: MIT

"""Sphinx configuration for the I2C microSD Expansion Arduino library."""

import datetime

extensions = ["sphinxcontrib.jquery"]

source_suffix = {".rst": "restructuredtext"}
master_doc = "index"

project = "I2C microSD Expansion Arduino Library"
author = "I2C microSD Expansion contributors"
creation_year = "2026"
current_year = str(datetime.datetime.now().year)
year_duration = (
    current_year if current_year == creation_year else creation_year + " - " + current_year
)
copyright = year_duration + " I2C microSD Expansion contributors"

version = "0.1.49"
release = "0.1.49"

language = "en"
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store", ".env", "CODE_OF_CONDUCT.md"]
pygments_style = "sphinx"
todo_include_todos = False

html_theme = "sphinx_rtd_theme"
htmlhelp_basename = "I2CmicroSDExpansionArduinodoc"

latex_documents = [
    (
        master_doc,
        "I2CmicroSDExpansionArduino.tex",
        "I2C microSD Expansion Arduino Library Documentation",
        author,
        "manual",
    )
]

man_pages = [
    (
        master_doc,
        "i2cmicrosdexpansionarduino",
        "I2C microSD Expansion Arduino Library Documentation",
        [author],
        1,
    )
]

texinfo_documents = [
    (
        master_doc,
        "I2CmicroSDExpansionArduino",
        "I2C microSD Expansion Arduino Library Documentation",
        author,
        "I2CmicroSDExpansionArduino",
        "Host library for the I2C microSD Expansion module.",
        "Miscellaneous",
    )
]
