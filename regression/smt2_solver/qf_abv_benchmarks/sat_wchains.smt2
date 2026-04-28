(set-info :smt-lib-version 2.6)
(set-logic QF_ABV)
(set-info :source |
This benchmark generates write chain permutations and tries to show
via extensionality that they are equal.

Contributed by Armin Biere (armin.biere@jku.at).
|)
(set-info :category "crafted")
(set-info :status sat)
(declare-fun a1 () (Array (_ BitVec 32) (_ BitVec 8)))
(declare-fun v6 () (_ BitVec 32))
(declare-fun v7 () (_ BitVec 32))
(assert (let ((?v_8 (bvadd (_ bv0 32) v6)) (?v_9 ((_ extract 7 0) v6)) (?v_10 (bvadd (_ bv1 32) v6)) (?v_11 ((_ extract 15 8) v6)) (?v_12 (bvadd (_ bv2 32) v6)) (?v_13 ((_ extract 23 16) v6)) (?v_14 (bvadd (_ bv3 32) v6)) (?v_15 ((_ extract 31 24) v6)) (?v_0 (bvadd (_ bv0 32) v7)) (?v_1 ((_ extract 7 0) v7)) (?v_2 (bvadd (_ bv1 32) v7)) (?v_3 ((_ extract 15 8) v7)) (?v_4 (bvadd (_ bv2 32) v7)) (?v_5 ((_ extract 23 16) v7)) (?v_6 (bvadd (_ bv3 32) v7)) (?v_7 ((_ extract 31 24) v7))) (not (= (bvnot (ite (= (store (store (store (store (store (store (store (store a1 ?v_8 ?v_9) ?v_10 ?v_11) ?v_12 ?v_13) ?v_14 ?v_15) ?v_0 ?v_1) ?v_2 ?v_3) ?v_4 ?v_5) ?v_6 ?v_7) (store (store (store (store (store (store (store (store a1 ?v_0 ?v_1) ?v_2 ?v_3) ?v_4 ?v_5) ?v_6 ?v_7) ?v_8 ?v_9) ?v_10 ?v_11) ?v_12 ?v_13) ?v_14 ?v_15)) (_ bv1 1) (_ bv0 1))) (_ bv0 1)))))
(check-sat)
(exit)
