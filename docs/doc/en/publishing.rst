Build and publish the manuals
=============================

Build and preview
-----------------

Use Python 3.11 or later and Make. Run these commands from the repository root.

.. code-block:: sh

   python3 -m venv docs/doc/.venv
   docs/doc/.venv/bin/python -m pip install -r docs/doc/requirements.txt
   make -C docs/doc html SPHINXBUILD=.venv/bin/sphinx-build
   python3 -m http.server 8000 --bind 127.0.0.1 --directory docs/doc/_build/html

Open `http://127.0.0.1:8000/ <http://127.0.0.1:8000/>`_ to choose a language.
The sources live in ``docs/doc/en/`` and ``docs/doc/ru/``. Keep page
basenames aligned so readers can switch languages from the same topic.
The HTML build treats Sphinx warnings as errors.

Publish on a static host
------------------------

Set ``PVLEDGER_DOCS_URL`` to the final public URL of the documentation root
to generate canonical URLs.

.. code-block:: sh

   PVLEDGER_DOCS_URL=https://docs.example.org/ make -C docs/doc html SPHINXBUILD=.venv/bin/sphinx-build

Deploy the entire ``docs/doc/_build/html/`` directory. Serve its root
``index.html`` and allow directory index pages at ``/en/`` and ``/ru/``.
The output also works at a path prefix such as
``https://example.org/manual/`` when the environment variable uses that base
URL. The site includes local search, screenshots and reference downloads.
A static file server is sufficient.
