(set-info :smt-lib-version 2.6)
(set-logic QF_ABV)
(set-info :source |
We verify that selection sort sorts an array
of length 5 in memory. Additionally, we read an element
at an arbitrary index of the initial array and show that this
element can not be unequal to an element in the sorted array.

Contributed by Robert Brummayer (robert.brummayer@gmail.com).
|)
(set-info :category "crafted")
(set-info :status unsat)
(declare-fun a2 () (Array (_ BitVec 32) (_ BitVec 8)))
(declare-fun start () (_ BitVec 32))
(declare-fun index () (_ BitVec 32))
(assert (let ((?v_35 (select a2 index)) (?v_4 (select a2 start)) (?v_2 (bvadd (_ bv1 32) start))) (let ((?v_3 (select a2 ?v_2))) (let ((?v_12 (= (_ bv1 1) (ite (bvult ?v_3 ?v_4) (_ bv1 1) (_ bv0 1))))) (let ((?v_6 (ite ?v_12 ?v_3 ?v_4)) (?v_1 (bvadd (_ bv1 32) ?v_2))) (let ((?v_5 (select a2 ?v_1))) (let ((?v_11 (= (_ bv1 1) (ite (bvult ?v_5 ?v_6) (_ bv1 1) (_ bv0 1))))) (let ((?v_8 (ite ?v_11 ?v_5 ?v_6)) (?v_0 (bvadd (_ bv1 32) ?v_1))) (let ((?v_7 (select a2 ?v_0))) (let ((?v_10 (= (_ bv1 1) (ite (bvult ?v_7 ?v_8) (_ bv1 1) (_ bv0 1)))) (?v_9 (bvadd (_ bv1 32) ?v_0))) (let ((?v_13 (ite (= (_ bv1 1) (ite (bvult (select a2 ?v_9) (ite ?v_10 ?v_7 ?v_8)) (_ bv1 1) (_ bv0 1))) ?v_9 (ite ?v_10 ?v_0 (ite ?v_11 ?v_1 (ite ?v_12 ?v_2 start)))))) (let ((?v_14 (store (store a2 start (select a2 ?v_13)) ?v_13 ?v_4))) (let ((?v_16 (select ?v_14 ?v_2)) (?v_15 (select ?v_14 ?v_1))) (let ((?v_20 (= (_ bv1 1) (ite (bvult ?v_15 ?v_16) (_ bv1 1) (_ bv0 1))))) (let ((?v_18 (ite ?v_20 ?v_15 ?v_16)) (?v_17 (select ?v_14 ?v_0))) (let ((?v_19 (= (_ bv1 1) (ite (bvult ?v_17 ?v_18) (_ bv1 1) (_ bv0 1))))) (let ((?v_21 (ite (= (_ bv1 1) (ite (bvult (select ?v_14 ?v_9) (ite ?v_19 ?v_17 ?v_18)) (_ bv1 1) (_ bv0 1))) ?v_9 (ite ?v_19 ?v_0 (ite ?v_20 ?v_1 ?v_2))))) (let ((?v_22 (store (store ?v_14 ?v_2 (select ?v_14 ?v_21)) ?v_21 ?v_16))) (let ((?v_24 (select ?v_22 ?v_1)) (?v_23 (select ?v_22 ?v_0))) (let ((?v_25 (= (_ bv1 1) (ite (bvult ?v_23 ?v_24) (_ bv1 1) (_ bv0 1))))) (let ((?v_26 (ite (= (_ bv1 1) (ite (bvult (select ?v_22 ?v_9) (ite ?v_25 ?v_23 ?v_24)) (_ bv1 1) (_ bv0 1))) ?v_9 (ite ?v_25 ?v_0 ?v_1)))) (let ((?v_27 (store (store ?v_22 ?v_1 (select ?v_22 ?v_26)) ?v_26 ?v_24))) (let ((?v_29 (select ?v_27 ?v_0))) (let ((?v_28 (ite (= (_ bv1 1) (ite (bvult (select ?v_27 ?v_9) ?v_29) (_ bv1 1) (_ bv0 1))) ?v_9 ?v_0))) (let ((?v_30 (store (store ?v_27 ?v_0 (select ?v_27 ?v_28)) ?v_28 ?v_29))) (let ((?v_34 (select ?v_30 start)) (?v_31 (select ?v_30 ?v_2)) (?v_32 (select ?v_30 ?v_1)) (?v_33 (select ?v_30 ?v_0)) (?v_36 (select ?v_30 ?v_9))) (not (= (bvnot (bvand (bvand (bvand (bvand (bvand (_ bv1 1) (bvnot (ite (bvult ?v_31 ?v_34) (_ bv1 1) (_ bv0 1)))) (bvnot (ite (bvult ?v_32 ?v_31) (_ bv1 1) (_ bv0 1)))) (bvnot (ite (bvult ?v_33 ?v_32) (_ bv1 1) (_ bv0 1)))) (bvnot (ite (bvult ?v_36 ?v_33) (_ bv1 1) (_ bv0 1)))) (bvnot (bvand (bvand (bvnot (ite (bvult index start) (_ bv1 1) (_ bv0 1))) (ite (bvult index (bvadd start (_ bv5 32))) (_ bv1 1) (_ bv0 1))) (bvand (bvand (bvand (bvand (bvand (_ bv1 1) (bvnot (ite (= ?v_35 ?v_34) (_ bv1 1) (_ bv0 1)))) (bvnot (ite (= ?v_35 ?v_31) (_ bv1 1) (_ bv0 1)))) (bvnot (ite (= ?v_35 ?v_32) (_ bv1 1) (_ bv0 1)))) (bvnot (ite (= ?v_35 ?v_33) (_ bv1 1) (_ bv0 1)))) (bvnot (ite (= ?v_35 ?v_36) (_ bv1 1) (_ bv0 1)))))))) (_ bv0 1)))))))))))))))))))))))))))))
(check-sat)
(exit)
