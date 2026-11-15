        ; cpu selected with -m68000
        section code
pc_hook:
        movem.l d0-d4/a0-a3,-(sp)
        movea.w #$e608,a0
        movea.w #$f400,a1
        moveq   #54,d1
        moveq   #0,d4
scan_list:
        moveq   #0,d2
        move.b  (a0),d2
        bmi.w   scan_done
        move.w  d2,d3
        lsl.w   #4,d3
        lea     (a1,d3.w),a3
        cmpi.w  #$ab1e,6(a3)
        beq.w   static_logo
        cmpi.w  #$b152,6(a3)
        beq.s   dynamic_logo
        cmpi.w  #$b5ea,6(a3)
        beq.s   dynamic_logo
        cmpi.w  #$ba82,6(a3)
        beq.s   dynamic_logo
        cmpi.w  #$bf1a,6(a3)
        beq.s   dynamic_logo
        cmpi.w  #$c3b2,6(a3)
        beq.s   dynamic_logo
        cmpi.w  #$c84a,6(a3)
        beq.s   dynamic_logo
        cmpi.w  #$cce1,6(a3)
        beq.s   dynamic_logo
        cmpi.w  #$d1cb,6(a3)
        beq.s   dynamic_logo
        bra.s   scan_next
static_logo:
        ori.w   #$c000,d4
        bra.s   scan_next
dynamic_logo:
        ori.w   #$8000,d4
        move.w  (a3),d0
        andi.w  #$00ff,d0
        cmpi.w  #$78,d0
        blo.s   scan_next
        addq.w  #1,d4
scan_next:
        adda.w  #6,a0
        dbra    d1,scan_list
scan_done:
        btst    #15,d4
        bne.s   logo_present
        btst    #14,d4
        bne.s   logo_present
        tst.w   $fff3fe
        beq.w   done
        tst.w   $fff3fc
        beq.w   clear_animation
        subq.w  #1,$fff3fc
        move.w  $fff3fe,d0
        cmpi.w  #81,d0
        bhs.s   hold_text
        addq.w  #1,d0
        move.w  d0,$fff3fe
        bra.w   draw_text
hold_text:
        bra.w   draw_text
clear_animation:
        clr.w   $fff3fe
        clr.w   $fff3fa
        bsr.w   clear_text_cells
        bra.w   done
logo_present:
        move.w  #300,$fff3fc
        move.w  $fff3fe,d0
        tst.w   d0
        bne.s   advance_animation
        move.w  d4,d0
        andi.w  #$3fff,d0
        cmpi.w  #7,d0
        blo.s   not_ready
        addq.w  #1,$fff3fa
        cmpi.w  #30,$fff3fa
        blo.s   keep_animation
        bra.s   begin_animation
not_ready:
        clr.w   $fff3fa
        bra.s   keep_animation
begin_animation:
        moveq   #1,d0
        bra.s   store_animation
advance_animation:
        cmpi.w  #81,d0
        bhs.s   store_animation
        addq.w  #1,d0
store_animation:
        move.w  d0,$fff3fe
        bra.w   draw_text
keep_animation:
        tst.w   $fff3fe
        beq.w   done
        bra.w   draw_text
draw_text:
        move.w  $fff3fe,d0
        cmpi.w  #1,d0
        bne.s   text_initialized
        lea     $fff3d0,a1
        moveq   #9,d3
init_text_cells:
        move.w  #$ffff,(a1)+
        dbra    d3,init_text_cells
text_initialized:
        move.w  $fff3fe,d0
        subq.w  #1,d0
        move.w  d0,d1
        lsl.w   #4,d1
        move.w  d0,d2
        lsl.w   #2,d2
        add.w   d2,d1
        lea     text_positions(pc),a0
        adda.w  d1,a0
        lea     $fff3d0,a1
        lea     character_codes(pc),a2
        moveq   #9,d3
draw_character:
        move.w  (a1),d0
        cmpi.w  #$ffff,d0
        beq.s   no_previous_cell
        movea.l #$00110000,a3
        adda.w  d0,a3
        clr.w   (a3)
no_previous_cell:
        move.w  (a0)+,d0
        move.w  d0,(a1)+
        moveq   #0,d1
        move.b  (a2)+,d1
        ori.w   #$8000,d1
        movea.l #$00110000,a3
        adda.w  d0,a3
        move.w  d1,(a3)
        dbra    d3,draw_character
        bra.w   done
clear_text_cells:
        lea     $fff3d0,a1
        moveq   #9,d3
clear_character:
        move.w  (a1),d0
        cmpi.w  #$ffff,d0
        beq.s   clear_next_cell
        movea.l #$00110000,a3
        adda.w  d0,a3
        clr.w   (a3)
clear_next_cell:
        move.w  #$ffff,(a1)+
        dbra    d3,clear_character
        rts
done:
        movem.l (sp)+,d0-d4/a0-a3
        movea.w #$e608,a0
        movea.w #$f400,a1
        rts
        even
character_codes:
        dc.b $50,$43,$20,$45,$44,$49,$54,$49,$4f,$4e
        even
text_positions:
        dc.w $05d8,$0550,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $05da,$05d2,$04cc,$044c,$034e,$02d4,$02dc,$0362,$0464,$0560
        dc.w $05dc,$05d4,$054e,$044c,$034c,$02d2,$02da,$0360,$0464,$0562
        dc.w $055e,$05d6,$0550,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2
        dc.w $0560,$05d8,$05d2,$04cc,$03cc,$034e,$02d4,$02dc,$0362,$0462
        dc.w $0560,$05dc,$05d4,$054e,$044c,$034c,$02d2,$02da,$0360,$0464
        dc.w $04e2,$055e,$05d6,$0550,$04cc,$03cc,$02d0,$02d8,$035e,$03e2
        dc.w $0462,$0560,$05d8,$05d2,$04cc,$03cc,$034e,$02d6,$02dc,$03e2
        dc.w $0464,$0560,$05da,$05d4,$054e,$044c,$034e,$02d4,$02da,$0360
        dc.w $03e2,$04e2,$05dc,$05d6,$054e,$044c,$03cc,$02d2,$02d8,$0360
        dc.w $03e2,$04e2,$055e,$05d8,$0550,$04cc,$03cc,$0350,$02d6,$02de
        dc.w $0360,$0464,$0560,$05da,$05d2,$054c,$044c,$034e,$02d4,$02dc
        dc.w $0360,$03e2,$04e2,$05dc,$05d4,$054e,$044c,$034c,$02d2,$02d8
        dc.w $02de,$03e2,$04e2,$055e,$05d8,$0550,$04cc,$03cc,$02d0,$02d6
        dc.w $02dc,$0362,$0464,$0560,$05da,$05d2,$054c,$044c,$034e,$02d4
        dc.w $02da,$0360,$0464,$04e2,$05dc,$05d4,$054e,$044c,$034c,$02d2
        dc.w $02d8,$02de,$03e2,$04e2,$055e,$05d8,$0550,$04cc,$03cc,$02d0
        dc.w $02d4,$02dc,$0362,$0464,$0560,$05da,$05d2,$04cc,$044c,$034e
        dc.w $02d2,$02da,$0360,$0464,$0562,$05dc,$05d4,$054e,$044c,$034c
        dc.w $02d0,$02d8,$02de,$03e2,$04e2,$055e,$05d6,$0550,$04cc,$03cc
        dc.w $034e,$02d4,$02dc,$0362,$0462,$0560,$05d8,$05d2,$04cc,$03cc
        dc.w $034c,$02d2,$02da,$0360,$0464,$0560,$05dc,$05d4,$054e,$044c
        dc.w $03cc,$02d0,$02d8,$035e,$03e2,$04e2,$055e,$05d6,$0550,$04cc
        dc.w $03cc,$034e,$02d6,$02dc,$03e2,$0462,$0560,$05d8,$05d2,$04cc
        dc.w $044c,$034e,$02d4,$02da,$0360,$0464,$0560,$05da,$05d4,$054e
        dc.w $044c,$03cc,$02d2,$02d8,$0360,$03e2,$04e2,$05dc,$05d6,$054e
        dc.w $04cc,$03cc,$0350,$02d6,$02de,$03e2,$04e2,$055e,$05d8,$0550
        dc.w $054c,$044c,$034e,$02d4,$02dc,$0360,$0464,$0560,$05da,$05d2
        dc.w $054e,$044c,$034c,$02d2,$02d8,$0360,$03e2,$04e2,$05dc,$05d4
        dc.w $0550,$04cc,$03cc,$02d0,$02d6,$02de,$03e2,$04e2,$055e,$05d8
        dc.w $05d2,$054c,$044c,$034e,$02d4,$02dc,$0362,$0464,$0560,$05da
        dc.w $05d4,$054e,$044c,$034c,$02d2,$02da,$0360,$0464,$04e2,$05dc
        dc.w $05d8,$0550,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $05d0,$0550,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0650,$0550,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0650,$0550,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $06d0,$0552,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $06d0,$05d2,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0750,$05d2,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0750,$0652,$04cc,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0750,$0652,$04d4,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $07d0,$06d2,$0554,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $07d0,$06d2,$0554,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0752,$05d4,$03cc,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0752,$05d4,$03d6,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$07d2,$0654,$0456,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$07d2,$0654,$0456,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$06d4,$04d6,$02d0,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0754,$0556,$02d8,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0754,$05d6,$0358,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$07d4,$0656,$03d8,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$07d4,$0656,$0458,$02d8,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$06d6,$04d8,$02da,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$0756,$0558,$035a,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$0756,$05d8,$03da,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$07d6,$0658,$045a,$02de,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$0658,$045a,$02dc,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$06d8,$055a,$035c,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$0758,$055a,$03dc,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$07d8,$05da,$045c,$03e2,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$065a,$04dc,$03de,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$06da,$055c,$045e,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$075a,$05dc,$045e,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$07da,$065c,$04de,$04e2,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$065c,$055e,$04e0,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$06dc,$05de,$0560,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$075c,$065e,$0560,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$07dc,$065e,$05e0,$055e
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$06de,$05e0,$0562
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$075e,$0660,$05e2
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$075e,$0660,$05e2
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$07de,$06e0,$0662
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$0760,$0662
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$0760,$06e2
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$07e0,$06e2
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$07e0,$0762
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$0860,$0762
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$0860,$07e2
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$0860,$07e2
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$0860,$0862
        dc.w $0850,$0852,$0854,$0856,$0858,$085a,$085c,$085e,$0860,$0862
