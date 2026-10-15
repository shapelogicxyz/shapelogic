// SPDX-License-Identifier: GPL-3.0-or-later
//
// A picture imported by its URL (`import x from './a.png?url'`): Vite
// bundles it and the import is its address. No imports here: this file
// must stay an ambient declaration.

declare module '*.png?url' {
  const url: string;
  export default url;
}
