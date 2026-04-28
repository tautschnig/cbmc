(set-info :smt-lib-version 2.6)
(set-logic QF_ABV)
(set-info :source |
We swap two byte sequences of length 4 twice in memory.
The sequences can overlap, hence it is not always the case
that swapping them twice yields the initial memory.

Swapping is done via XOR in the following way:
x ^= y;
y ^= x;
x ^= y;

Contributed by Robert Brummayer (robert.brummayer@gmail.com).
|)
(set-info :category "crafted")
(set-info :status sat)
(declare-fun a1 () (Array (_ BitVec 32) (_ BitVec 8)))
(declare-fun start1 () (_ BitVec 32))
(declare-fun start2 () (_ BitVec 32))
(assert (let ((?v_0 (select a1 start1)) (?v_1 (select a1 start2))) (let ((?v_2 (bvnot ?v_1))) (let ((?v_3 (bvand (bvnot (bvand (bvnot ?v_0) ?v_2)) (bvnot (bvand ?v_0 ?v_1))))) (let ((?v_4 (bvnot ?v_3))) (let ((?v_5 (bvand (bvnot (bvand ?v_2 ?v_4)) (bvnot (bvand ?v_1 ?v_3))))) (let ((?v_6 (store (store (store a1 start1 ?v_3) start2 ?v_5) start1 (bvand (bvnot (bvand ?v_4 (bvnot ?v_5))) (bvnot (bvand ?v_3 ?v_5))))) (?v_7 (bvadd start1 (_ bv1 32))) (?v_10 (bvadd start2 (_ bv1 32)))) (let ((?v_8 (select ?v_6 ?v_7)) (?v_9 (select ?v_6 ?v_10))) (let ((?v_11 (bvnot ?v_9))) (let ((?v_12 (bvand (bvnot (bvand (bvnot ?v_8) ?v_11)) (bvnot (bvand ?v_8 ?v_9))))) (let ((?v_13 (bvnot ?v_12))) (let ((?v_14 (bvand (bvnot (bvand ?v_11 ?v_13)) (bvnot (bvand ?v_9 ?v_12))))) (let ((?v_15 (store (store (store ?v_6 ?v_7 ?v_12) ?v_10 ?v_14) ?v_7 (bvand (bvnot (bvand ?v_13 (bvnot ?v_14))) (bvnot (bvand ?v_12 ?v_14))))) (?v_16 (bvadd (_ bv1 32) ?v_7)) (?v_19 (bvadd (_ bv1 32) ?v_10))) (let ((?v_17 (select ?v_15 ?v_16)) (?v_18 (select ?v_15 ?v_19))) (let ((?v_20 (bvnot ?v_18))) (let ((?v_21 (bvand (bvnot (bvand (bvnot ?v_17) ?v_20)) (bvnot (bvand ?v_17 ?v_18))))) (let ((?v_22 (bvnot ?v_21))) (let ((?v_23 (bvand (bvnot (bvand ?v_20 ?v_22)) (bvnot (bvand ?v_18 ?v_21))))) (let ((?v_24 (store (store (store ?v_15 ?v_16 ?v_21) ?v_19 ?v_23) ?v_16 (bvand (bvnot (bvand ?v_22 (bvnot ?v_23))) (bvnot (bvand ?v_21 ?v_23))))) (?v_25 (bvadd (_ bv1 32) ?v_16)) (?v_28 (bvadd (_ bv1 32) ?v_19))) (let ((?v_26 (select ?v_24 ?v_25)) (?v_27 (select ?v_24 ?v_28))) (let ((?v_29 (bvnot ?v_27))) (let ((?v_30 (bvand (bvnot (bvand (bvnot ?v_26) ?v_29)) (bvnot (bvand ?v_26 ?v_27))))) (let ((?v_31 (bvnot ?v_30))) (let ((?v_32 (bvand (bvnot (bvand ?v_29 ?v_31)) (bvnot (bvand ?v_27 ?v_30))))) (let ((?v_33 (store (store (store ?v_24 ?v_25 ?v_30) ?v_28 ?v_32) ?v_25 (bvand (bvnot (bvand ?v_31 (bvnot ?v_32))) (bvnot (bvand ?v_30 ?v_32)))))) (let ((?v_34 (select ?v_33 start1)) (?v_35 (select ?v_33 start2))) (let ((?v_36 (bvnot ?v_35))) (let ((?v_37 (bvand (bvnot (bvand (bvnot ?v_34) ?v_36)) (bvnot (bvand ?v_34 ?v_35))))) (let ((?v_38 (bvnot ?v_37))) (let ((?v_39 (bvand (bvnot (bvand ?v_36 ?v_38)) (bvnot (bvand ?v_35 ?v_37))))) (let ((?v_40 (store (store (store ?v_33 start1 ?v_37) start2 ?v_39) start1 (bvand (bvnot (bvand ?v_38 (bvnot ?v_39))) (bvnot (bvand ?v_37 ?v_39)))))) (let ((?v_41 (select ?v_40 ?v_7)) (?v_42 (select ?v_40 ?v_10))) (let ((?v_43 (bvnot ?v_42))) (let ((?v_44 (bvand (bvnot (bvand (bvnot ?v_41) ?v_43)) (bvnot (bvand ?v_41 ?v_42))))) (let ((?v_45 (bvnot ?v_44))) (let ((?v_46 (bvand (bvnot (bvand ?v_43 ?v_45)) (bvnot (bvand ?v_42 ?v_44))))) (let ((?v_47 (store (store (store ?v_40 ?v_7 ?v_44) ?v_10 ?v_46) ?v_7 (bvand (bvnot (bvand ?v_45 (bvnot ?v_46))) (bvnot (bvand ?v_44 ?v_46)))))) (let ((?v_48 (select ?v_47 ?v_16)) (?v_49 (select ?v_47 ?v_19))) (let ((?v_50 (bvnot ?v_49))) (let ((?v_51 (bvand (bvnot (bvand (bvnot ?v_48) ?v_50)) (bvnot (bvand ?v_48 ?v_49))))) (let ((?v_52 (bvnot ?v_51))) (let ((?v_53 (bvand (bvnot (bvand ?v_50 ?v_52)) (bvnot (bvand ?v_49 ?v_51))))) (let ((?v_54 (store (store (store ?v_47 ?v_16 ?v_51) ?v_19 ?v_53) ?v_16 (bvand (bvnot (bvand ?v_52 (bvnot ?v_53))) (bvnot (bvand ?v_51 ?v_53)))))) (let ((?v_55 (select ?v_54 ?v_25)) (?v_56 (select ?v_54 ?v_28))) (let ((?v_57 (bvnot ?v_56))) (let ((?v_58 (bvand (bvnot (bvand (bvnot ?v_55) ?v_57)) (bvnot (bvand ?v_55 ?v_56))))) (let ((?v_59 (bvnot ?v_58))) (let ((?v_60 (bvand (bvnot (bvand ?v_57 ?v_59)) (bvnot (bvand ?v_56 ?v_58))))) (not (= (bvnot (ite (= a1 (store (store (store ?v_54 ?v_25 ?v_58) ?v_28 ?v_60) ?v_25 (bvand (bvnot (bvand ?v_59 (bvnot ?v_60))) (bvnot (bvand ?v_58 ?v_60))))) (_ bv1 1) (_ bv0 1))) (_ bv0 1)))))))))))))))))))))))))))))))))))))))))))))))))))
(check-sat)
(exit)
