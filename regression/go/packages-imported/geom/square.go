package geom

type Square struct{ Side int }

func (s *Square) Area() int { return s.Side * s.Side }

func init() { Unit.W = 1 }
