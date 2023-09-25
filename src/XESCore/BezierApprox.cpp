/* 
 * Copyright (c) 2011, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
 * THE SOFTWARE.
 *
 */

#include "BezierApprox.h"
#include "AssertUtils.h"
#include "MathUtils.h"
#include "GISTool_Globals.h"
#include "XESConstants.h"
#include "PlatformUtils.h"

/*

	A few implementation notes:
	
	SEQUENCES:
	
	To work on point sequences, we introduce the "sequence" concept.  A sequence gives us a series of points with these operators:

		*x			give us the current point.
		++x			go to the next point.
		x()			return true if we are at the end of sequence.  When x() then *x is invalid.
		
	We can then wrap one sequence inside another using various adaptors.  The advance of sequences over iterators is that we don't need to
	generate an "end" adapter for an end iterator.

	INDEXING:
	
	The main "error" test we use is to find the distance of a point P to a sequence X.  This runs in linear time.  We then find the
	variance in distance between sequences X and Y by calculating the variance of every one of Y's points P along X.  This is of
	course N^2, which just sucks.  (But since bezier curves don't "jump" in even, predictable ways, there's not much we can do about this.
	We could in theory subdivide the curves based on some kind of length-along metric, but this would get expensive.)
	
	Since we are going to compare one "master" curve M to many guesses G, we can spatially index the segments on M, and then run points on
	G into the spatial index M.  This gives us len(G) log(len(M)) which is acceptable.
	
	In our case, our spatial index is really stupidly simple: 
	1. We pick a single axis to index along, based on the larger axis of the AABB of the sequence.
	2. We break the sequence into sub-sequences that are monotone along that axis.
	3. We store each sub-sequence in ascending value order.
	
	Note that this means that the interior vertices where monotone changes are stored twice - once at the end of one sequence and once at
	the beginning of the other.
	
	Searching within a window is then pretty easily: for each monotone chain, we take the lower bound of our search window along the index
	axis and then walk forward until we run out the window.  In practice, we will typically only have a few matching segments for reasonably
	well-aligned beziers.
	
	The nice thing about this system is that we can build it using vectors, so we get good local coherency (each monotone chain is contiguous
	in memory) and low overhead (we store only the actual sorted points).  For our use our chains often have a very small number of points 
	(e.g. 20-30) so constant-time factors really matter.  Comparison numbers at KSAN (simplify all roads, full optimization):
	
	- No index: 83 seconds
	- CGAL R-trees using cartesian-double: 150 seconds
	- sorted vector monotone index: 11 seconds

	BEZIER SCRUBBING
	
	The fundamental algorithm observation is this: if we are going to preserve the continuous derivative of a piece-wise bezier curve, the 
	approximation's tangents at the non-removed points can change only in magnitude, not direction (because the next separate piece-wise
	curve might not change in the same way if direction is up for grabs).  
	
	Therefore, given a piece-wise bezier curve, we can attempt to approximate it by rescaling the distance of the first and last control 
	points along the line from the ends to the control points, while dropping the rest.
	
	We can measure the error as the variance of the rasterized approximate curve to the rasterized original.  (Why not the Hausdorff 
	distance?  I wanted something that captures the net effect of _all_ points; Hausdorff is based on the worst case error - if one part
	of the curve is bad, we can't "rank" how much better the rest of the curve is doing.)
	
	The algorithm thus works as a bottom-up approximator:
	
	1. We Create a priority queue of possible merges for every interior point on the curve, queued by error.

	2. We merge the two curves that produce minimum error, and re-prioritize our two neighbors (whose error may now be larger since one half
	of their merge is the result of our prior merge.
	
	3. When we exceed our error bounds or the queue is empty, we're done.
	
	Why not something more like Douglas-Peuker?  Well, DP starts with the WORST approximation and adds points back.  The problem with this
	is that for very long piece-wise curves we're going to have to do a lot of error measurement against the ENTIRE curve up front just to
	discover that yes, we DO need MOST of the nodes.  Since curve compare is linaer with the number of pieces, consider the effect of this
	when we have N curves and NO merges will happen.
	
	Bottom-up: we do N-1 error measurements, each with 2 curves and stop.  Cost: O(2N-2)
	DP: we do N-1 vertex inserts.  Each one will try every possible vertex (to find the worst error).  Just to add the first point we
		will do N(N-1) measurements.  This will then happen again for N/2, N/4, etc.  So we're looking at (N^2-N)logN.

	The reason this is so much worse than regular DP is because we don't have a constant-time error metric.
	
*/

// Use spatial indexing?  KEEP THIS ON!  HUGE speed win!
#define INDEXED 1

// Debugging - steps the UI version through the curve process.
#define DEBUG_CURVE_FIT_TRIALS 0
#define DEBUG_CURVE_INDEX 0
#define DEBUG_CURVE_FIT_SOLUTION 0
#define DEBUG_MERGE 0
#define DEBUG_START_END 0

inline bool ray_intersect(const Point2& p1, const Vector2& v1, const Point2& p2, const Vector2& v2, double& t1, double& t2)
{
	double det = v2.dx * v1.dy - v2.dy * v1.dx;
	if(det < 0.0) 
		return false;

	double dx = p2.x() - p1.x();
	double dy = p2.y() - p1.y();
	t1 = (dy * v2.dx - dx * v2.dy) / det;
	t2 = (dy * v1.dx - dx * v1.dy) / det;
	return true;
}

template<typename __Iter>
void visualize_bezier_seq(__Iter first, __Iter last, int r, int g, int b)
{
	while(first != last)
	{
		debug_mesh_point(*first,r,g,b);
		DebugAssert(!first->c);
		__Iter stop = first;
		++stop;
		while(stop->c) ++stop;
		
		int d = distance(first,stop);
		switch(d) {
		case 1:
			debug_mesh_line(*first, *stop, r,g,b,r,g,b);
			break;
		case 2:
			debug_mesh_bezier(*first, *nth_from(first,1), *nth_from(first,2), r,g,b,r,g,b);
			break;
		case 3: 
			debug_mesh_bezier(*first, *nth_from(first,1), *nth_from(first,2),*nth_from(first,3), r,g,b,r,g,b);
			break;
		}
		first = stop;
		
	}
	debug_mesh_point(*last,r,g,b);
	
}

#if INDEXED

typedef	std::pair<std::list<vector<Point2> >, bool>		PolyLineIndex;

bool is_reverse_x(const Point2& p1, const Point2& p2, const Point2& p3)
{
	double dx1 = p2.x() - p1.x();
	double dx2 = p3.x() - p2.x();
	if(dx1 > 0.0 && dx2 < 0.0) return true;
	if(dx1 < 0.0 && dx2 > 0.0) return true;
	return false;
}

bool is_reverse_y(const Point2& p1, const Point2& p2, const Point2& p3)
{
	double dy1 = p2.y() - p1.y();
	double dy2 = p3.y() - p2.y();
	if(dy1 > 0.0 && dy2 < 0.0) return true;
	if(dy1 < 0.0 && dy2 > 0.0) return true;
	return false;
}

template <class __Seq>
void make_index_seq(__Seq seq, PolyLineIndex& out_index)
{
	out_index.first.clear();
	
	vector<Point2>	all;
	DebugAssert(!seq());
	Bbox2			bbox;
	while(!seq())
	{	
		all.push_back(*seq);
		bbox += *seq;
		++seq;
	}
	DebugAssert(all.size() > 1);
	if(bbox.xspan() > bbox.yspan())
	{
		// SORT BY X
		out_index.second = false;
		int span_start = 0;
		while(span_start < all.size())
		{
			int span_stop = span_start + 1;
			DebugAssert(span_stop != all.size());
			++span_stop;
			while(span_stop < all.size() && !is_reverse_x(all[span_stop-2],all[span_stop-1],all[span_stop])) 
				++span_stop;

			out_index.first.push_back(vector<Point2>(all.begin()+span_start, all.begin()+span_stop));
			DebugAssert((span_stop-span_start) > 1);
			if (span_stop == all.size()) break;
			span_start = span_stop-1;
		}
		for(std::list<vector<Point2> >::iterator ps = out_index.first.begin(); ps != out_index.first.end(); ++ps)
		{
			bool is_rev = false;
			for(int n = 1; n < ps->size(); ++n)
			if(ps->at(n-1).x() > ps->at(n).x())
			{
				is_rev = true;
				break;
			}
			if(is_rev)
				reverse(ps->begin(),ps->end());
		}
	} 
	else
	{
		// SORT BY Y
		out_index.second = true;
		int span_start = 0;
		while(span_start < all.size())
		{
			int span_stop = span_start + 1;
			DebugAssert(span_stop != all.size());
			++span_stop;
			while(span_stop < all.size() && !is_reverse_y(all[span_stop-2],all[span_stop-1],all[span_stop])) 
				++span_stop;

			out_index.first.push_back(vector<Point2>(all.begin()+span_start, all.begin()+span_stop));
			DebugAssert((span_stop-span_start) > 1);
			if (span_stop == all.size()) break;
			span_start = span_stop-1;
		}
		for(std::list<vector<Point2> >::iterator ps = out_index.first.begin(); ps != out_index.first.end(); ++ps)
		{
			bool is_rev = false;
			for(int n = 1; n < ps->size(); ++n)
			if(ps->at(n-1).y() > ps->at(n).y())
			{
				is_rev = true;
				break;
			}
			if(is_rev)
				reverse(ps->begin(),ps->end());
		}
	}
	
}

double squared_distance_pt_seq(PolyLineIndex& iseq, const Point2& p, double max_err)
{
	double worst = max_err * 10.0;
	for(std::list<vector<Point2> >::iterator ps = iseq.first.begin(); ps != iseq.first.end(); ++ps)
	{
		if(iseq.second)
		{
			vector<Point2>::iterator start = lower_bound(ps->begin(),ps->end(), Point2(p.y(),p.y()-max_err), lesser_y());
			if(start != ps->end())
			{
				vector<Point2>::iterator prev(start);
				++start;
				if(start != ps->end())
				do
				{
					Segment2 seg(*prev, *start);
					worst = min(worst,seg.squared_distance(p));		
					if((start->y() - max_err) > p.y())
						break;
					prev = start;
					++start;
				} while(start != ps->end());
			}
		}
		else
		{
			vector<Point2>::iterator start = lower_bound(ps->begin(),ps->end(), Point2(p.x()-max_err,p.y()), lesser_x());			
			if(start != ps->end())
			{				
				vector<Point2>::iterator prev(start);
				++start;
				if(start != ps->end())
				do
				{
					Segment2 seg(*prev, *start);
					worst = min(worst,seg.squared_distance(p));		
					if((start->x() - max_err) > p.x())
						break;
					prev = start;
					++start;
				} while(start != ps->end());
			}
		}
	}
	return worst;

	
}

template <class __Seq>
double squared_distance_seq_seq(PolyLineIndex& s2, __Seq s1, double max_err)
{
	double t = 0.0;
	double v = 0.0;
	while(!s1())
	{
		Point2 p = *s1;
		++s1;
//		worst = max(worst, squared_distance_pt_seq<__Seq2>(s2, p));
		v += squared_distance_pt_seq(s2, p, max_err);
		++t;
	}
	return sqrt(v) / t;
}


#else

template <class __Seq>
double squared_distance_pt_seq(__Seq seq, const Point2& p)
{
	if(seq()) return 0.0;
	Segment2 s;
	s.p1 = *seq;
	++seq;
	if(seq()) return 0.0;
	s.p2 = *seq;
	++seq;
		
	double worst = s.squared_distance(p);
	while(!seq())
	{
		s.p1 = s.p2;
		s.p2 = *seq;
		++seq;
		worst = min(worst,s.squared_distance(p));		
	}
	return worst;
}

template <class __Seq1, class __Seq2>
double squared_distance_seq_seq(__Seq1 s1, __Seq2 s2)
{
	double t = 0.0;
	double v = 0.0;
	while(!s1())
	{
		Point2 p = *s1;
		++s1;
//		worst = max(worst, squared_distance_pt_seq<__Seq2>(s2, p));
		v += squared_distance_pt_seq<__Seq2>(s2, p);
		++t;
	}
	return sqrt(v) / t;
}


#endif

template <class __Seq>
struct bezier_approx_seq {

	bezier_approx_seq(__Seq in_seq, bool in_want_last) : t(0.0), want_last(in_want_last), s(in_seq), done(false)
	{
		b.p2 = *s;
		++s;	
		if(s())
		{
			b.c1 = b.c2 = b.p1 = b.p2;
			done = true;
			t = 0.0;
		}
		else
			advance();
	}

	Point2 operator*()
	{
		return b.midpoint(t);
	}
	
	bool operator()(void)
	{
		if(!done) return false;
		if(!s())	  return false;
		if(want_last) return false;
		return true;
	}
	bezier_approx_seq& operator++(void)
	{		
		if(done && want_last)		// We're done, but last point isn't consumed?  Mark as consumed
			want_last = false;
		else if (t < 1.0)				// else we have a bezier....advance the T if possible
		{
			t += 0.125;
		}
		else						// else go to next in pt seq
		{
			if(s())					// Note: if sequence is tapped out, go to "done" state!
			{
				b.c1 = b.c2 = b.p1 = b.p2;
				done = true;
				t = 0.0;
			}
			else
				advance();
		}
		return *this;
	}

private:

	void advance(void)
	{
		Point2c np1 = *s;
		++s;
		if(np1.c)
		{
			Point2c np2 = *s;
			++s;
			if(np2.c)
			{
				Point2c np3 = *s;
				++s;
				b = Bezier2(b.p2,np1,np2,np3);
			}
			else
			{
				b = Bezier2(b.p2,np1,np2);
			}
		}
		else
		{
			b.p1 = b.p2;
			b.p2 = np1;
			b.c1 = b.p1;
			b.c2 = b.p2;
		}
		
		t = 0.0;
	}


	Bezier2		b;
	double		t;
	__Seq		s;

	bool		want_last;
	bool		done;			// This indicates that we have already emitted the last real curve
								// of the bezier and we are sitting "on" the end-point.

};

template <class Iter>
struct seq_for_container {

	Iter begin;
	Iter end;
	seq_for_container(Iter b, Iter e) : begin(b), end(e) { }
	
	bool operator()(void) { return begin == end; }
	Point2c operator*() { return *begin; }
	seq_for_container& operator++(void) { ++begin; return *this; }
};

template <typename S1, typename S2>
struct seq_concat {
	S1	s1;
	S2	s2;
	seq_concat(const S1& is1, const S2& is2) : s1(is1), s2(is2) { }
	bool operator()(void) { return s1() && s2(); }
	Point2c operator*() { return s1() ? *s2 : *s1; }
	seq_concat& operator++(void) { if(s1()) ++s2; else ++s1; return *this; } 
};

typedef std::list<Point2c>	bez_list;

#if INDEXED
template <typename T>
double error_for_approx(PolyLineIndex& index, T s2_begin, T s2_end, double max_err)
{
	typedef seq_for_container<T>	TS;
	typedef bezier_approx_seq<TS>	TAS;
		
	TS ts(s2_begin,s2_end);
	TAS	tas(ts,true);

	return squared_distance_seq_seq(index, tas, max_err);
}
#else

template <typename T1, typename T2>
double error_for_approx(T1 s1_begin, T1 s1_end, T2 s2_begin, T2 s2_end)
{
	typedef seq_for_container<T1>	T1S;
	typedef seq_for_container<T2>	T2S;
	typedef bezier_approx_seq<T1S>	T1AS;
	typedef bezier_approx_seq<T2S>	T2AS;
		
	T1S t1s(s1_begin,s1_end);
	T2S	t2s(s2_begin,s2_end);
	T1AS	t1as(t1s,true);
	T2AS	t2as(t2s,true);

	return squared_distance_seq_seq(t1as, t2as);
}
#endif

double best_bezier_approx(
					std::list<Point2c>::iterator orig_first,
					std::list<Point2c>::iterator orig_last,
					Point2c			approx[4],
					double&			 t1_best,
					double&			 t2_best,
					double			 frac_ratio,
					int				 step_start,
					int				 step_stop,
					double			max_err)
{
	DebugAssert(orig_last != orig_first);
	std::list<Point2c>::iterator orig_c1(orig_first); ++orig_c1;
	DebugAssert(orig_c1 != orig_last);
	std::list<Point2c>::iterator orig_c2(orig_last); --orig_c2;
	DebugAssert(orig_c2 != orig_first);
	DebugAssert(!orig_first->c);
	DebugAssert(!orig_last->c);
	DebugAssert(orig_c1->c);
	DebugAssert(orig_c2->c);
	std::list<Point2c>::iterator orig_end(orig_last);
	++orig_end;
	
	#if INDEXED
	PolyLineIndex	orig_index;

	typedef seq_for_container<std::list<Point2c>::iterator>	orig_seq_type;
	typedef bezier_approx_seq<orig_seq_type>			approx_seq_type;
		
	make_index_seq<approx_seq_type>(approx_seq_type(orig_seq_type(orig_first,orig_end),true),orig_index);
	
	#if DEBUG_CURVE_INDEX
	
	if(orig_index.first.size() > 1)
	{
		gMeshLines.clear();
		gMeshPoints.clear();
		gMeshBeziers.clear();
		for (std::list<vector<Point2> >::iterator ps = orig_index.first.begin(); ps != orig_index.first.end(); ++ps)
		{
			for(int n = 1; n < ps->size(); ++n)
			{
				debug_mesh_point(ps->at(n),0.2,0.2,0.2);		
				debug_mesh_line(ps->at(n-1),ps->at(n),
					orig_index.second ? 1.0 : 0.0,1,interp(0,0,ps->size()-1,1,n-1),
					orig_index.second ? 1.0 : 0.0,1,interp(0,0,ps->size()-1,1,n  ));
			}	
			debug_mesh_point(ps->front(),1,1,1);
			debug_mesh_point(ps->back(),1,1,1);
		}
		DoUserAlert("Index");
	}
	#endif
	
	
	#endif
		
	DebugAssert(*orig_first != *orig_c1);	
	DebugAssert(*orig_last != *orig_c2);	
	
	Vector2	c1v(approx[0],approx[1]);
	Vector2	c2v(approx[3],approx[2]);
		
//	TODO
	
	double err = 0.0;
	bool has_err = false;
		
	Point2c	this_approx[4];
	
	double t1_orig(t1_best);
	double t2_orig(t2_best);
	
	for(int s1 = step_start; s1 <= step_stop; ++s1)	
	for(int s2 = step_start; s2 <= step_stop; ++s2)	
	{
		double t1 = t1_orig * pow(frac_ratio, s1);
		double t2 = t2_orig * pow(frac_ratio, s2);
//		double t1 = double_interp(0,t1_lo,steps-1,t1_hi,s1);
//		double t2 = double_interp(0,t2_lo,steps-1,t2_hi,s2);
		this_approx[0] = *orig_first;
		this_approx[1] = Point2c(*orig_first + c1v * t1,true);
		this_approx[2] = Point2c(*orig_last + c2v * t2,true);
		this_approx[3] = *orig_last;		

		#if INDEXED
		double my_err = fabs(error_for_approx<Point2c*>(orig_index, this_approx,this_approx+4, max_err));
		#else
		double my_err = fabs(error_for_approx<std::list<Point2c>::iterator,Point2c*>(orig_first, orig_end, this_approx,this_approx+4));
		#endif

		#if DEBUG_CURVE_FIT_TRIALS
		gMeshBeziers.clear();
		gMeshPoints.clear();

		visualize_bezier_seq(orig_first, orig_last,0,1,0);
		if(!has_err || my_err < err)
			visualize_bezier_seq(this_approx,this_approx+3, 1,1,0);
		else {
			visualize_bezier_seq(this_approx,this_approx+3, 1,0,0);
			visualize_bezier_seq(approx,approx+3, 1,1,0);
		}
		debug_mesh_point(this_approx[1],1,0,0);
		debug_mesh_point(this_approx[2],0,1,0);

		printf("trial with t1=%lf, t2=%lf, err=%lf\n", t1, t2, my_err*DEG_TO_MTR_LAT);
		DoUserAlert("Trial");
		#endif
		if(!has_err || my_err < err)
		{
			has_err = true;
			err = my_err;
			approx[0] = this_approx[0];
			approx[1] = this_approx[1];
			approx[2] = this_approx[2];
			approx[3] = this_approx[3];
			t1_best = t1;
			t2_best = t2;
		}
	}
	
	#if DEBUG_CURVE_FIT_SOLUTION
	
	gMeshBeziers.clear();
	gMeshPoints.clear();
	visualize_bezier_seq(orig_first, orig_last,0,1,0);
	debug_mesh_point(approx[1],1,0,0);
	debug_mesh_point(approx[2],0,1,0);
	debug_mesh_bezier(approx[0],approx[1],approx[2],approx[3],1,1,1, 1,1,1);
	printf("Best err: %lf with t1=%lf,t2=%lf\n",err*DEG_TO_MTR_LAT, t1_best,t2_best);
	DoUserAlert("Best");
	#endif
	return err;
}

struct possible_approx_t;

struct	approx_t {
	approx_t *				prev;			// Links to prev/next approx in our chain
	approx_t *				next;
	bez_list::iterator		orig_first;		// List iterator to original span of nodes, INCLUSIVE! REALLY!
	bez_list::iterator		orig_last;
	Point2c					approx[4];		// Four bezier nodes approximate the curve

	possible_approx_t *		merge_left;		// ptr into future approximations, so that if
	possible_approx_t *		merge_right;	// we are merged out, we can "find" ourselves.
};

typedef multimap<double, possible_approx_t *>	possible_approx_q;

struct	possible_approx_t {
	approx_t *					left;
	approx_t *					right;
	Point2c						approx[4];
	double						err;
	possible_approx_q::iterator	self;
};

void setup_approx(approx_t * l, approx_t * r, possible_approx_t * who, possible_approx_q * q, double err_lim)
{
	DebugAssert(l->next == r);
	DebugAssert(r->prev == l);
	l->merge_right = who;
	r->merge_left = who;
	who->left = l;
	who->right = r;
	
	double t1 = 1.0, t2 = 1.0;
	who->approx[0] = l->approx[0];
	who->approx[1] = l->approx[1];
	who->approx[2] = r->approx[2];
	who->approx[3] = r->approx[3];
	
	who->err = best_bezier_approx(l->orig_first, r->orig_last, who->approx, 
						t1, t2, 2.0, -1, 3, err_lim);

	// We have to RESET the approx - our big win is that our t1/t2 are now LOOSELY calibrated.
	// So the bezier approx must start over or we double-apply the approx.
	who->approx[0] = l->approx[0];
	who->approx[1] = l->approx[1];
	who->approx[2] = r->approx[2];
	who->approx[3] = r->approx[3];

	who->err = best_bezier_approx(l->orig_first, r->orig_last, who->approx, 
						t1, t2, 1.22, -2, 2, err_lim);

	if(q)
		who->self = q->insert(possible_approx_q::value_type(who->err, who));
}


// Apply the merge described by "who" - when done, the right edge (and who)
// are gone, and the new edge is returned.
approx_t * merge_approx(possible_approx_t * who, possible_approx_q * q, double err_lim)
{
	approx_t * l = who->left;
	approx_t * r = who->right;
	DebugAssert(l->next == r);
	DebugAssert(r->prev == l);
	DebugAssert(l->merge_right == who);
	DebugAssert(r->merge_left == who);
	
	l->next = r->next;
	if(l->next)
		l->next->prev = l;
	l->orig_last = r->orig_last;
	l->merge_right = r->merge_right;
	if(l->merge_right)
		l->merge_right->left = l;
	
	for(int n = 0; n < 4; ++n)
		l->approx[n] = who->approx[n];
	
	if(q && who->self != q->end())
		q->erase(who->self);
	
	delete r;
	delete who;

	if(l->merge_left)
	{
		DebugAssert(l->prev);
		if(q && l->merge_left->self != q->end())
			q->erase(l->merge_left->self);		
		setup_approx(l->prev, l, l->merge_left, q, err_lim);
			
	}
	if(l->merge_right)
	{
		DebugAssert(l->next);
		if(q && l->merge_right->self != q->end())
			q->erase(l->merge_right->self);		
		setup_approx(l, l->next, l->merge_right, q, err_lim);
	}
	
	#if DEBUG_MERGE
	gMeshBeziers.clear();
	gMeshPoints.clear();
	visualize_bezier_seq(l->orig_first,l->orig_last,0,1,0);
	visualize_bezier_seq(l->approx,l->approx+3,1,1,0);
	DoUserAlert("Merged");
	#endif
	
	return l;
	
	
}

void bezier_multi_simplify(
					std::list<Point2c>::iterator	first,
					std::list<Point2c>::iterator	last,
					std::list<Point2c>&			simplified,
					double					max_err,
					double					lim_err)
{
	#if DEBUG_START_END
	gMeshBeziers.clear();
	gMeshPoints.clear();
	visualize_bezier_seq(first, last, 0,1,0);
	DoUserAlert("Will simplify this curve.");
	#endif
	approx_t *				orig = NULL;
	possible_approx_q 		q;
	std::list<Point2c>::iterator	start, stop;
	/* STEP 1 - build an approx std::list for each bezier curve in the original sequence. */

	start = first;
	approx_t * prev = NULL;
	DebugAssert(!last->c);
	while(start != last)
	{
		DebugAssert(!start->c);
		stop = nth_from(start,1);
		while(stop->c) ++stop;
		
		approx_t * seg = new approx_t;
		seg->prev = prev;
		if(prev) prev->next = seg;
		else orig = seg;
		seg->next = NULL;
		seg->orig_first = start;
		seg->orig_last = stop;
		seg->merge_left = seg->merge_right = NULL;
		
		int dist = distance(start,stop);
		DebugAssert(dist > 1);
		DebugAssert(dist < 4);
		
		if(dist == 2)
		{
			Bezier2 app(*start, *nth_from(start,1),*stop);
			seg->approx[0] = app.p1;
			seg->approx[1] = app.c1;
			seg->approx[2] = app.c2;
			seg->approx[3] = app.p2;
		}
		else if(dist == 3)
		{
			seg->approx[0] = *start;
			seg->approx[1] = *nth_from(start,1);
			seg->approx[2] = *nth_from(stop,-1);
			seg->approx[3] = *stop;
		}
		start = stop;
		prev = seg;
	}
	
	/* Step 2 - build a possible approx for each adjacent PAIR of approximations. */
	
	approx_t * seg;
	DebugAssert(orig);
	for(seg = orig; seg->next; seg = seg->next)
	{
		possible_approx_t * app = new possible_approx_t;
		setup_approx(seg, seg->next, app, &q, lim_err);		
	}
	
	/* Step 3 - run the Q to do the actual merges. */
	while(!q.empty())
	{
		if(q.begin()->first > max_err)	
			break;

		possible_approx_t * who = q.begin()->second;
		merge_approx(who, &q, lim_err);						
	}
	
	/* Step 4 - output the final approxiamte sequence! */
	while(!q.empty())
	{
		delete q.begin()->second;
		q.erase(q.begin());
	}

	simplified.clear();
	while(orig)
	{
		simplified.insert(simplified.end(),orig->approx, orig->approx+3);
		if(orig->next == NULL)
			simplified.push_back(orig->approx[3]);
		approx_t * k = orig;
		orig = orig->next;
		delete k;
	}
	
	#if DEBUG_START_END
	gMeshBeziers.clear();
	gMeshPoints.clear();
	visualize_bezier_seq(first, last, 0,1,0);
	visualize_bezier_seq(simplified.begin(),nth_from(simplified.end(),-1),1,1,1);
	DoUserAlert("End result.");
	#endif
	
}

void bezier_multi_simplify_straight_ok(
					std::list<Point2c>&		seq,
					double				max_err,
					double				lim_err)
{
	std::list<Point2c>::iterator start(seq.begin()), stop, last(seq.end());
	--last;

	DebugAssert(!last->c);
	while(start != last)
	{
		DebugAssert(!start->c);
		stop = start;
		int ctr = 1;
		int curves = 1;
		++stop;
		if(stop->c)
		{
			while(stop->c) ++stop, ++ctr;
			while(stop != last && nth_from(stop,1)->c)
			{
				DebugAssert(!stop->c);
				++stop;
				++curves;
				while(stop->c) ++stop, ++ctr;
			}
			DebugAssert(!stop->c);
			DebugAssert(stop == last || !nth_from(stop,1)->c);
			
			if(curves > 1)
			{
				std::list<Point2c>	better;
				bezier_multi_simplify(start,stop,better,max_err, lim_err);
				
				if(ctr >= better.size())				// old has to be SMALLER since it isn't counting its end node!
				{
					seq.erase(nth_from(start,1),stop);	// erase between start & stop
					seq.splice(nth_from(start,1),better,nth_from(better.begin(),1),nth_from(better.end(),-1));
				}
			}
		}
		start = stop;
	}
}
