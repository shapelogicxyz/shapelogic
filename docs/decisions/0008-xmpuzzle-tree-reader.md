# 0008: Read .xmpuzzle files into a tree, keep what is not modelled

**Status:** accepted (stage 2); `core/bt/io/`, `core/bt/model/xml_element.h`

## Context
Decision 0006 asks that content the new core does not model (stacking rod
sets, elements added by later versions) is written back unchanged. The
original read files with a pull parser and skipped every element it did not
know (`skipSubTree`), so porting its loaders as they were would lose that
content. Its parser also has behaviour files depend on: the five named
entities and numeric references, text merged across comments and CDATA,
`\r\n` folded to `\n`, a newline in an attribute value read as a space,
whatever follows the root element left unread.

## Decision
- **The original's parser, ported as it is** (`xml_pull_parser.{h,cpp}`,
  from kXML2 via wsdlpull), builds an `XmlElement` tree
  (`xml_document.h`): name, attributes in file order, child elements, and
  the text of leaf elements. Text standing between child elements is
  dropped (the format has no mixed content). The builder is iterative and
  refuses documents nested more than 5000 deep, because the model readers
  and the tree's destructor recurse.
- **The model readers walk the tree in file order** and apply the
  original's checks, messages and number parsing (`atoi` is `strtol` cast
  to `int`, so odd values read the same). An error names the line of the
  offending element.
- **Unknown content is kept where a model object owns it**
  (`PreservedXml`): unknown attributes and child elements of `<puzzle>`,
  `<problem>` and `<solution>`, and unknown attributes of `<voxel>` (on
  `Voxel`). Each kept child remembers its *slot*: the writer position of
  the last known sibling before it, so it is written back in the same
  place. Unknown content deeper inside other known elements (`<colors>`,
  a part's `<shape>`, `<bitmap>`, `<state>`...) is dropped, as the
  original drops it.
- **Stacking is kept, not modelled**: `<puzzle>/<rodSets>` and
  `<problem>/<stacking>` are preserved elements. A stacking puzzle without
  rod sets is written with the default one, as the original does.
  (Amended by decision 0015, deferred part 3a: they are modelled now, read
  and written as the original did.)
- **The writer is a port of `xmlWriter_c`** and the save functions write in
  the original's order, so saves are byte-identical to the original's (365
  of 365 files in `just oracle-roundtrip`).
- **Errors inside the module are exceptions, at its edge `Result`s.** The
  parser (`XmlParseError`) and the readers (a local `ReadError`) throw, as
  the original's did, which keeps their code close to it. Both types are
  private to `bt/io`'s sources and are caught in `parseXmlDocument()` and
  `readPuzzle()`; the public functions return `Result<T>` with
  `Errc::ParseError` or `Errc::IoError`, so decision 0005 holds at the
  module boundary.

## Consequences
- Files from later versions keep their extra content through a save, as
  long as it sits at puzzle, problem or solution level (where new features
  have added it so far).
- The whole file is in memory twice while loading (the tree, then the
  model). The largest corpus file (65902 disassembly states) loads in a
  fraction of a second; a streaming reader can come back if a file ever
  makes this matter.
- Editing shapes of a stacking puzzle does not update the preserved
  `<stacking>` element (the original renumbered its rods); the stacking
  port has to model it. (Done in deferred part 3a: decision 0015.)
