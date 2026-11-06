<p align="center">
  <img src="assets/appicon/mentis-128.png" alt="Mentis" width="72">
</p>

<h1 align="center">Contributing to Mentis</h1>

<p align="center"><a href="README.md">← Back to the README</a></p>

Thank you for wanting to help! Every contribution is welcome: bug reports, ideas, translations,
documentation and code. Please read the [Code of Conduct](CODE_OF_CONDUCT.md) first.

## Report a bug

Open an [issue](https://github.com/simozock/Mentis/issues/new/choose) with the **Bug report** form:
what you did, what happened, what you expected, your Mentis version and operating system. A small
`.mentis` file or a screenshot that shows the problem helps a lot.

For a security problem, do not open a public issue: follow [SECURITY.md](SECURITY.md).

## Propose a feature

Open an issue with the **Feature request** form, or start a thread in
[Discussions](https://github.com/simozock/Mentis/discussions). The roadmap is community-driven: the
most requested features are added to it and developed according to the requests and the time of the
developers.

## Translate

Each language is one plain JSON file in [translations/](translations/README.md): the English text
and its translation, side by side. Edit it with any text editor, or in the browser on GitHub, and
open a pull request. A language appears in Mentis when it is at least 80% translated; the missing
texts stay in English.

## Write documentation

The user manual is in [manual/](manual/README.md), one short page per feature, in Markdown; the rules
are in [manual/STYLE.md](manual/STYLE.md). Corrections,
clearer explanations, screenshots and short GIFs are welcome.

## Contribute code

1. Open an issue first for anything larger than a small fix, so we can agree on the behaviour before
   you write the code.
2. Build and run the tests as described in [BUILD.md](BUILD.md).
3. Keep the change focused: one fix or one feature per pull request, with a test that fails without
   it.
4. Follow the existing style: C++20, Qt 6 Widgets, the repository's `.clang-format`, comments in
   English, short and specific.
5. Every text shown in the interface is written in English in `tr()`; after
   `cmake --build build --target update_translations` the new texts appear, empty, in the JSON files
   of `translations/` (add the Italian one in `it.json` if you can).
6. Mentis must stay fast on old and low-power PCs: avoid work repeated at every repaint and explain
   any change that affects performance.
7. Open the pull request against `main`, describing what changes and how you tested it.

By contributing you agree that your contribution is released under the project's license,
[GNU AGPL-3.0](LICENSE).

## Use of AI

AI tools were used to build Mentis and you may use them too, as long as you understand, test and
take responsibility for what you submit. Do not submit code, texts or images copied from other
products: Mentis is developed in a clean room, without reverse engineering.
