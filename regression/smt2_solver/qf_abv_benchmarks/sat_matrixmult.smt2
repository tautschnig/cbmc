(set-info :smt-lib-version 2.6)
(set-logic QF_ABV)
(set-info :source |
This benchmark shows that matrix multiplicationis not commutative in general.
We try to show that A x B = B x A, which is generally not the case.
The matrices have 2 * 2 fields of bit-width 32,
and are respectively represented by a one-dimensional array.

Contributed by Robert Brummayer (robert.brummayer@gmail.com).
|)
(set-info :category "crafted")
(set-info :status sat)
(declare-fun a5 () (Array (_ BitVec 2) (_ BitVec 32)))
(declare-fun a6 () (Array (_ BitVec 2) (_ BitVec 32)))
(declare-fun a7 () (Array (_ BitVec 2) (_ BitVec 32)))
(declare-fun a49 () (Array (_ BitVec 2) (_ BitVec 32)))
(assert (let ((?v_0 (store (store (store (store a7 (_ bv0 2) (_ bv0 32)) (_ bv1 2) (_ bv0 32)) (_ bv2 2) (_ bv0 32)) (_ bv3 2) (_ bv0 32))) (?v_3 (select a5 (_ bv0 2))) (?v_7 (select a6 (_ bv0 2)))) (let ((?v_16 (bvmul ?v_3 ?v_7))) (let ((?v_1 (bvadd (select ?v_0 (_ bv0 2)) ?v_16)) (?v_5 (select a5 (_ bv1 2))) (?v_9 (select a6 (_ bv2 2)))) (let ((?v_24 (bvmul ?v_5 ?v_9))) (let ((?v_2 (store (store ?v_0 (_ bv0 2) ?v_1) (_ bv0 2) (bvadd ?v_1 ?v_24))) (?v_11 (select a6 (_ bv1 2)))) (let ((?v_4 (bvadd (select ?v_2 (_ bv1 2)) (bvmul ?v_3 ?v_11))) (?v_14 (select a6 (_ bv3 2)))) (let ((?v_6 (store (store ?v_2 (_ bv1 2) ?v_4) (_ bv1 2) (bvadd ?v_4 (bvmul ?v_5 ?v_14)))) (?v_12 (select a5 (_ bv2 2)))) (let ((?v_8 (bvadd (select ?v_6 (_ bv2 2)) (bvmul ?v_7 ?v_12))) (?v_15 (select a5 (_ bv3 2)))) (let ((?v_10 (store (store ?v_6 (_ bv2 2) ?v_8) (_ bv2 2) (bvadd ?v_8 (bvmul ?v_9 ?v_15)))) (?v_18 (bvmul ?v_11 ?v_12))) (let ((?v_13 (bvadd (select ?v_10 (_ bv3 2)) ?v_18)) (?v_26 (bvmul ?v_14 ?v_15)) (?v_17 (store (store (store (store a49 (_ bv0 2) (_ bv0 32)) (_ bv1 2) (_ bv0 32)) (_ bv2 2) (_ bv0 32)) (_ bv3 2) (_ bv0 32)))) (let ((?v_19 (bvadd ?v_16 (select ?v_17 (_ bv0 2))))) (let ((?v_20 (store (store ?v_17 (_ bv0 2) ?v_19) (_ bv0 2) (bvadd ?v_18 ?v_19)))) (let ((?v_21 (bvadd (select ?v_20 (_ bv1 2)) (bvmul ?v_7 ?v_5)))) (let ((?v_22 (store (store ?v_20 (_ bv1 2) ?v_21) (_ bv1 2) (bvadd ?v_21 (bvmul ?v_11 ?v_15))))) (let ((?v_23 (bvadd (select ?v_22 (_ bv2 2)) (bvmul ?v_3 ?v_9)))) (let ((?v_25 (store (store ?v_22 (_ bv2 2) ?v_23) (_ bv2 2) (bvadd ?v_23 (bvmul ?v_14 ?v_12))))) (let ((?v_27 (bvadd ?v_24 (select ?v_25 (_ bv3 2))))) (not (= (bvnot (ite (= (store (store ?v_10 (_ bv3 2) ?v_13) (_ bv3 2) (bvadd ?v_13 ?v_26)) (store (store ?v_25 (_ bv3 2) ?v_27) (_ bv3 2) (bvadd ?v_26 ?v_27))) (_ bv1 1) (_ bv0 1))) (_ bv0 1)))))))))))))))))))))
(check-sat)
(exit)
