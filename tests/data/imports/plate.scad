include <puzzlecad.scad>

// a comment with burr_plate( inside it
/* and a block
   comment burr_piece("xxx") */
$burr_scale = 15.7;

burr_plate($burr_inset = 0.07, $plate_width = [1, [2, 3]], [
    [ "xx.|.xx", "x{label_text=A}..|..." ],
    "xxx|x.x" "|xxx",
    [ "x\"x" ],
]);

*burr_piece("xxxxxxxx");

burr_piece([ "x.x|xxx", ".x.|..." ], $joint_inset = 0.01);

packing_box([ "xxx|xxx|xxx", "xxx|x.x|xxx" ], $box_wall_thickness = 4);
