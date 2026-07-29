// NextTotalwar Greenfield — 400x400m 手工维护主战场。
use <../../lib/kit_layout.scad>
use <../../lib/kit_terrain.scad>
use <../../lib/kit_overhill.scad>
use <../../lib/kit_tw.scad>
$fn=8;

TERR=["gkterr1",[400,400],[176,176],731,[0,2.2,0.45],undef,"temperate",[
    ["ridge",[[-190,120],[-90,150],[-20,130]],60,18],
    ["ridge",[[40,-150],[140,-120],[195,-80]],55,15],
    ["plateau",[-60,-40],45,5],
    ["river",[[10,200],[-20,60],[-10,-60],[20,-200]],9,1.6],
    ["road",[[-200,-20],[-60,0],[60,10],[200,30]],6],
    ["road",[[-200,82],[-80,78],[40,72],[200,66]],5],
    ["pad",[-150,60],[40,30],0],
    ["pad",[150,-50],[36,26],0]
]];

gk_terrain(TERR);

// 两座桥，长度覆盖河岸下切带；锚点取河外路面。
translate([-13,4,gk_terrain_height(TERR,-28,4)]) rotate([0,0,5]) oh_prop_bridge(L=24,W=6,seed=1);
translate([-17,75,gk_terrain_height(TERR,-32,75)]) rotate([0,0,-3]) oh_prop_bridge(L=24,W=5.5,seed=2);

// 西村。
ter_place(TERR,-160,65) rotate([0,0,12]) tw_bldg_hut(1);
ter_place(TERR,-146,68) rotate([0,0,-8]) oh_bldg_cabin(seed=2,L=6,D=4.5);
ter_place(TERR,-135,58) tw_bldg_watchtower(3);
ter_place(TERR,-150,49) tw_bldg_palisade(26,2.5,4);
ter_place(TERR,-169,53) rotate([0,0,90]) tw_bldg_palisade(22,2.5,5);
ter_place(TERR,-144,56) tw_prop_cart(6);
ter_place(TERR,-157,55) tw_prop_haystack(7);

// 东村。
ter_place(TERR,143,-44) rotate([0,0,175]) tw_bldg_hut(8);
ter_place(TERR,157,-49) rotate([0,0,188]) oh_bldg_cabin(seed=9,L=6,D=4.5);
ter_place(TERR,164,-59) tw_bldg_watchtower(10);
ter_place(TERR,150,-63) tw_bldg_palisade(27,2.5,11);
ter_place(TERR,135,-51) rotate([0,0,90]) tw_bldg_palisade(20,2.5,12);
ter_place(TERR,150,-52) tw_prop_banner(13,[0.62,0.13,0.10]);

// 边缘林地与山脊岩块；中央 200x160m 保持开阔。
ter_scatter(TERR,101,72,[-196,-196,-105,196],[0.2,35,28,3,["grass","grass_dark"]],
            rot=true,dz=0,variants=1,scale=[1,1])
    lay_pick($seed) {
        oh_nature_pine(s=lay_randr($seed,5,1.2,2.0),seed=$seed);
        oh_nature_autumn(s=lay_randr($seed,6,1.1,1.8),seed=$seed);
    }
ter_scatter(TERR,211,64,[105,-196,196,196],[0.2,35,28,3,["grass","grass_dark"]],
            rot=true,dz=0,variants=1,scale=[1,1])
    lay_pick($seed) {
        oh_nature_pine(s=lay_randr($seed,7,1.2,2.1),seed=$seed);
        oh_nature_bush(s=lay_randr($seed,8,1.0,1.5),seed=$seed);
    }
ter_scatter(TERR,307,32,[-196,-196,196,196],[2,40,60,1,["rock","rock_high"]],
            rot=true,dz=0,variants=0,scale=[1,1])
    oh_rock_boulder(s=lay_randr($seed,4,0.8,1.7),seed=$seed);
