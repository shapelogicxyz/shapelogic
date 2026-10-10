// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walls of a sliding puzzle's tray in the Solver's view (docs/webui-
// spec.md, section 8.3): ported from the original's voxelframe.cpp,
// `buildSlidingWalls` (the outline of the floor, a little outside it so
// it does not fight with the faces of a piece pushed against it: a
// straight run moves one gap, a corner one gap along each axis; posts
// where the outline turns) and `drawSlidingWalls` (the outline at the
// floor's bottom and top, the posts between, black). DOM-free.

/** The outline: segments x0,y0,x1,y1 in tray coordinates, and posts x,y. */
export interface WallOutline {
  lines: number[];
  posts: number[];
}

/** How far the walls stand outside the floor (buildSlidingWalls's gap). */
export const WALL_GAP = 0.08;

/**
 * The outline of a tray's floor (a cell is open where any layer has floor;
 * `floor(x, y)` answers inside the tray only).
 */
export function wallOutline(
  sx: number,
  sy: number,
  floor: (x: number, y: number) => boolean,
): WallOutline {
  const floorAt = (x: number, y: number): boolean =>
    x >= 0 && y >= 0 && x < sx && y < sy && floor(x, y);
  // a grid corner moves away from the wall cells around it
  const corner = (vx: number, vy: number): { x: number; y: number; walls: number } => {
    let dx = 0;
    let dy = 0;
    let w = 0;
    for (let cy = vy - 1; cy <= vy; cy++) {
      for (let cx = vx - 1; cx <= vx; cx++) {
        if (!floorAt(cx, cy)) {
          dx += cx + 0.5 - vx;
          dy += cy + 0.5 - vy;
          w++;
        }
      }
    }
    const k = w === 2 ? WALL_GAP : 2 * WALL_GAP;
    return { x: vx + dx * k, y: vy + dy * k, walls: w };
  };
  const lines: number[] = [];
  const edge = (x0: number, y0: number, x1: number, y1: number): void => {
    const a = corner(x0, y0);
    const b = corner(x1, y1);
    lines.push(a.x, a.y, b.x, b.y);
  };
  for (let y = 0; y < sy; y++) {
    for (let x = 0; x < sx; x++) {
      if (!floorAt(x, y)) {
        continue;
      }
      if (!floorAt(x - 1, y)) {
        edge(x, y, x, y + 1);
      }
      if (!floorAt(x + 1, y)) {
        edge(x + 1, y, x + 1, y + 1);
      }
      if (!floorAt(x, y - 1)) {
        edge(x, y, x + 1, y);
      }
      if (!floorAt(x, y + 1)) {
        edge(x, y + 1, x + 1, y + 1);
      }
    }
  }
  // posts where the outline turns, so the top and bottom read as a wall
  const posts: number[] = [];
  for (let vy = 0; vy <= sy; vy++) {
    for (let vx = 0; vx <= sx; vx++) {
      const c = corner(vx, vy);
      if (c.walls === 1 || c.walls === 3) {
        posts.push(c.x, c.y);
      }
    }
  }
  return { lines, posts };
}

/**
 * The walls as 3D line segments (x,y,z pairs) in the solution scene's
 * frame (mesh units centred on the result of size sx, sy, sz): the outline
 * at the floor's bottom and its top, and the posts between.
 */
export function wallSegments(o: WallOutline, sx: number, sy: number, sz: number): Float32Array {
  const cx = sx / 2;
  const cy = sy / 2;
  const cz = sz / 2;
  const out: number[] = [];
  for (const z of [0, sz]) {
    for (let i = 0; i + 3 < o.lines.length; i += 4) {
      out.push(
        (o.lines[i] ?? 0) - cx,
        (o.lines[i + 1] ?? 0) - cy,
        z - cz,
        (o.lines[i + 2] ?? 0) - cx,
        (o.lines[i + 3] ?? 0) - cy,
        z - cz,
      );
    }
  }
  for (let i = 0; i + 1 < o.posts.length; i += 2) {
    const x = (o.posts[i] ?? 0) - cx;
    const y = (o.posts[i + 1] ?? 0) - cy;
    out.push(x, y, -cz, x, y, sz - cz);
  }
  return new Float32Array(out);
}
