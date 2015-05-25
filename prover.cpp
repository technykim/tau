//#define READLINE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
#include <iostream>
#include <ostream>
#include "misc.h"
#include <utility>
#include "object.h"
#include "prover.h"
#include <iterator>
#include <forward_list>
#include <boost/algorithm/string.hpp>

using namespace boost::algorithm;
bidict& gdict = dict;

int logequalTo, lognotEqualTo, rdffirst, rdfrest, A, rdfsResource, rdfList, Dot, GND, rdfsType, rdfssubClassOf;

int _indent = 0;
namespace prover {

rule* rules = 0;
proof* proofs = 0;
uint nrules, nproofs;

void printterm_substs(termid p, const subst& s);
string format(termid p);
string format(const ruleset& rs);
void printr(int r, const ruleset& kb);
void printp(proof* p, const ruleset& kb);
void printrs(const ruleset& rs);
void printl(const termset& l);
string format(const termset& l);
void printe(const evidence& e, const ruleset& kb);
void prints(const subst& s);
string format(int r, const ruleset& kb);

std::vector<term> term::terms;

std::list<string> proc;
string indent() {
	if (!_indent) return string();
	std::wstringstream ss;
	for (auto it = proc.rbegin(); it != proc.rend(); ++it) {
		string str = L"(";
		str += *it;
		str += L") ";
		ss << std::setw(8) << str;
	}
	ss << "    " << std::setw(_indent * 2);
	return ss.str();
}

struct _setproc {
	string prev;
	_setproc(const string& p) {
		prover::proc.push_front(p);
	}
	~_setproc() {
		prover::proc.pop_front();
	}
};

#ifdef DEBUG
#define setproc(x) _setproc __setproc(x)
#else
#define setproc(x)
#endif

void initmem() {
	static bool once = false;
	if (!once) {
		rules = new rule[max_rules];
		proofs = new proof[max_proofs];
		once = true;
	}
	nrules = nproofs = 0;
}

bool hasvar(termid id) {
	const term& p = term::get(id);
	setproc(L"evaluate");
	if (p.p < 0) return true;
	if (!p.s && !p.o) return false;
	return hasvar(p.s) || hasvar(p.o);
}

termid evaluate(termid id, const subst& s) {
	setproc(L"evaluate");
	termid r;
	const term& p = term::get(id);
	if (p.p < 0) {
		auto it = s.find(p.p);
		r = it == s.end() ? 0 : evaluate(it->second, s);
	} else if (!p.s && !p.o)
		r = id;
	else {
		termid a = evaluate(p.s, s);
		termid b = evaluate(p.o, s);
		r = term::make(p.p, a ? a : term::make(term::get(p.s).p), b ? b : term::make(term::get(p.o).p));
	}
	TRACE(printterm_substs(id, s); dout << " = "; if (!r) dout << "(null)"; else printterm(r); dout << std::endl);
	return r;
}

bool maybe_unify(termid _s, termid _d) {
	if (!_s != !_d) return false;
	if (!_s) return true;
	const term& s = term::get(_s);
	const term& d = term::get(_d);
	if (s.p < 0 || d.p < 0) return true;
	if (!(s.p == d.p && !s.s == !d.s && !s.o == !d.o)) return false;
	return !s.s || (maybe_unify(s.s, d.s) && maybe_unify(s.o, d.o));
}

bool unify(termid _s, const subst& ssub, termid _d, subst& dsub, bool f) {
	setproc(L"unify");
	termid v;
	const term& s = term::get(_s);
	const term& d = term::get(_d);
	bool r, ns = false;
	if (s.p < 0) 
		r = (v = evaluate(_s, ssub)) ? unify(v, ssub, _d, dsub, f) : true;
	else if (d.p < 0) {
		if ((v = evaluate(_d, dsub)))
			r = unify(_s, ssub, v, dsub, f);
		else {
			if (f) {
				dsub[d.p] = evaluate(_s, ssub);
				ns = true;
			}
			r = true;
		}
	} else if (!(s.p == d.p && !s.s == !d.s && !s.o == !d.o))
		r = false;
	else
		r = !s.s || (unify(s.s, ssub, d.s, dsub, f) && unify(s.o, ssub, d.o, dsub, f));
	TRACE(dout << "Trying to unify ";
		printterm_substs(_s, ssub);
		dout<<" with ";
		printterm_substs(_d, dsub);
		dout<<"... ";
		if (r) {
			dout << "passed";
			if (ns) {
				dout << " with new substitution: " << dstr(d.p) << " / ";
				printterm(dsub[d.p]);
			}
		} else dout << "failed";
		dout << std::endl);
	return r;
}

bool euler_path(proof* p, termid t, const ruleset& kb, bool update = false) {
	proof* ep = p;
	while ((ep = ep->prev))
		if (!kb.head[ep->rul] == !t &&
			unify(kb.head[ep->rul], ep->s, t, p->s, update))
			break;
	if (ep) { TRACE(dout<<"Euler path detected\n"); }
	return ep;
}

int builtin(termid id, session& ss) {
	setproc(L"builtin");
	const term& t = term::get(id);
	int r = -1;
	proof& p = *ss.p;
	termid i0 = t.s ? evaluate(t.s, p.s) : 0;
	termid i1 = t.o ? evaluate(t.o, p.s) : 0;
	const term* t0 = i0 ? &term::get(evaluate(t.s, p.s)) : 0;
	const term* t1 = i1 ? &term::get(evaluate(t.o, p.s)) : 0;
	if (t.p == GND) r = 1;
	else if (t.p == logequalTo)
		r = t0 && t1 && t0->p == t1->p ? 1 : 0;
	else if (t.p == lognotEqualTo)
		r = t0 && t1 && t0->p != t1->p ? 1 : 0;
	else if (t.p == rdffirst && t0 && t0->p == Dot && (t0->s || t0->o))
		r = unify(t0->s, p.s, t.o, p.s, true) ? 1 : 0;
	else if (t.p == rdfrest && t0 && t0->p == Dot && (t0->s || t0->o))
		r = unify(t0->o, p.s, t.o, p.s, true) ? 1 : 0;
	else if (t.p == A || t.p == rdfsType || t.p == rdfssubClassOf) {
		if (!t0) t0 = &term::get(t.s);
		if (!t1) t1 = &term::get(t.o);
		static termid vs = term::make(L"?S");
		static termid va = term::make(L"?A");
		static termid vb = term::make(L"?B");
//		rule* rl = &rules[nrules++];
		int rl = ss.kb.head.size();
		static termid h = term::make ( A,              vs, vb );
//		const rule* rl = *ss.kb[_rl->p->p].insert(_rl).first;
		proof* f = &proofs[nproofs++];
		f->rul = rl;
		f->prev = &p;
		f->last = 0;//ss.kb.body[rl].begin();
		f->g = p.g;
		if (euler_path(f, h, ss.kb))
			return -1;
		ss.kb.head.push_back(h);
		ss.kb.body.push_back(termset());
		ss.kb.body[rl].push_back ( term::make ( rdfssubClassOf, va, vb ) );
		ss.kb.body[rl].push_back ( term::make ( A,              vs, va ) );
		ss.q.push_back(f);
		TRACE(dout<<"builtin created frame:"<<std::endl;printp(f, ss.kb));
		r = -1;
	}
	if (r == 1) {
		proof* r = &proofs[nproofs++];
//		rule* rl = &rules[nrules++];
//		rl->p = evaluate(t, p.s);
		uint sz = ss.kb.head.size();
		ss.kb.head.push_back(evaluate(id, p.s));
		ss.kb.body.push_back(termset());
		*r = p;
		TRACE(dout << "builtin added rule: "; printr(sz, ss.kb);
			dout << " by evaluating "; printterm_substs(id, p.s); dout << std::endl);
		r->g = p.g;
		r->g.emplace_back(sz, subst());
		++r->last;
		ss.q.push_back(r);
	}

	return r;
}

std::set<uint> match(termid e, session& ss) {
	std::set<uint> m;
	uint n = 0;
	for (auto r : ss.kb.head) {
		if (r && maybe_unify(e, r))
			m.insert(n);
		++n;
	}
	return m;
}

void prove(session& ss) {
	++_indent;
	setproc(L"prove");
	termset& goal = ss.goal;
//	ruleset& cases = ss.kb;
//	rule* rg = &rules[nrules++];
	ss.p = &proofs[nproofs++];
//	rg->p = 0;
//	rg->body(goal);
	uint sz = ss.kb.head.size();
	ss.kb.head.push_back(0);
	ss.kb.body.push_back(goal);
	ss.p->rul = sz;
	ss.p->last = 0;//ss.kb.body[sz].begin();//rg->body().begin();
	ss.p->prev = 0;
	ss.q.push_back(ss.p);
	TRACE(dout << KRED << "Facts:\n";);
	TRACE(printrs(ss.kb));
	TRACE(dout << KGRN << "Query: "; printl(goal); dout << KNRM << std::endl);
	++_indent;
	do {
		ss.p = ss.q.back();
		ss.q.pop_back();
		proof& p = *ss.p;
		TRACE(dout<<"popped frame:\n";printp(&p, ss.kb));
		if (p.callback.target<int(struct session&)>() && !p.callback(ss)) continue;
		if (p.last != ss.kb.body[p.rul].size() && !euler_path(&p, ss.kb.head[p.rul], ss.kb)) {
			termid t = ss.kb.body[p.rul][p.last];
			if (!t) throw 0;
			TRACE(dout<<"Tracking back from ";
				printterm(t);
				dout << std::endl);
			if (builtin(t, ss) != -1) continue;

			for (auto rl : match(evaluate(t, p.s), ss)) {
				subst s;
				termid h = ss.kb.head[rl];
				if (!unify(t, p.s, h, s, true))
					continue;
				proof* r = &proofs[nproofs++];
				r->rul = rl;
				r->last = 0;//ss.kb.body[rl].begin();//r->rul->body().begin();
				r->prev = &p;
				r->g = p.g;
				r->s = s;
				if (ss.kb.body[rl].empty())
					r->g.emplace_back(rl, subst());
				ss.q.push_front(r);
			}
		}
		else if (!p.prev) {
			for (auto r = ss.kb.body[p.rul].begin(); r != ss.kb.body[p.rul].end(); ++r) {
				termid t = evaluate(*r, p.s);
				if (!t || hasvar(t)) continue;
				TRACE(dout << "pushing evidence: " << format(t) << std::endl);
				ss.e[term::get(t).p].emplace_back(t, p.g);
			}
		//	ss.q.clear();
		} else {
			ground g = p.g;
			proof* r = &proofs[nproofs++];
			if (!ss.kb.body[p.rul].empty())
				g.emplace_back(p.rul, p.s);
			*r = *p.prev;
			r->g = g;
			r->s = p.prev->s;
			unify(ss.kb.head[p.rul], p.s, ss.kb.body[r->rul][r->last], r->s, true);
			++r->last;
			ss.q.push_back(r);
			continue;
		}
	} while (!ss.q.empty());
	--_indent;
	--_indent;
	TRACE(dout << KWHT << "Evidence:" << std::endl; 
		printe(ss.e, ss.kb); dout << KNRM);
}


string format(termid id) {
	const term& p = term::get(id);
	std::wstringstream ss;
	if (p.s)
		ss << format (p.s);
	ss << L' ' << dstr(p.p) << L' ';
	if (p.o)
		ss << format (p.o);
	return ss.str();
}

//bool equals(termid x, termid y) { if (!x) return !y; if (x == y) return true; if (x->p == y->p && x->s == y->s && x->p == x->p) return true; return format(*x) == format(*y); }
void printterm(termid p) { dout << format(p); }
void printg(const ground& g, const ruleset& kb);

void printp(proof* p, const ruleset& kb) {
	if (!p) return;
	dout << KCYN;
	dout << indent() << L"rule:   ";
	printr(p->rul, kb);
	dout<<std::endl<<indent();
	if (p->prev)
		dout << L"prev:   " << p->prev <<std::endl<<indent()<< L"subst:  ";
	else
		dout << L"prev:   (null)"<<std::endl<<indent()<<"subst:  ";
	prints(p->s);
	dout <<std::endl<<indent()<< L"ground: " << std::endl;
	++_indent;
	printg(p->g, kb);
	--_indent;
	dout << L"\n";
	dout << KNRM;
}

void printrs(const ruleset& rs) {
	dout << format(rs);
}

void printq(const queue& q, const ruleset& kb) {
	for (auto x : q) {
		printp(x, kb);
		dout << std::endl;
	}
}

void prints(const subst& s) {
	for (auto x : s) {
		dout << dstr(x.first) << L" / ";
		printterm(x.second);
		dout << ' ';// << std::endl;
	}
}

void printl(const termset& l) { dout << format(l); }
string format(const termset& l) {
	std::wstringstream ss;
	auto x = l.begin();
	while (x != l.end()) {
		ss << format (*x);
		if (++x != l.end())
			ss << L',';
	}
	return ss.str();
}


void printterm_substs(termid id, const subst& s) {
	const term& p = term::get(id);
	if (p.s) {
		printterm_substs(p.s, s);
		dout << L' ';
	}
	dout << dstr(p.p);
	if(s.find(p.p) != s.end()) {
		dout << L" (";
		printterm(s.at(p.p));
		dout << L" )";
	}
	if (p.o) {
		dout << L' ';
		printterm_substs(p.o, s);
	}
}

void printl_substs(const termset& l, const subst& s) {
	auto x = l.begin();
	while (x != l.end()) {
		printterm_substs(*x, s);
		if (++x != l.end())
			dout << L',';
	}
}

void printr_substs(int r, const subst& s, const ruleset& kb) {
	printl_substs(kb.body[r], s);
	dout << L" => ";
	printterm_substs(kb.head[r], s);
}

string format(int r, const ruleset& kb) {
	std::wstringstream ss;
	if(!kb.body[r].empty()) ss << format(kb.body[r]) << L" => ";
	if (kb.head[r]) 	ss << format(kb.head[r]);
	if(kb.body[r].empty()) 	ss << L".";
	return ss.str();
}

string format(const ruleset& rs) {
	std::wstringstream ss;
	uint n = 0;
	auto ith = rs.head.begin();
	auto itb = rs.body.begin();
	for (auto it = rs.head.begin(); it != rs.head.end(); ++it) {
		if (!itb->empty()) 	ss << format(*itb) << L" => ";
		if (*ith) 		ss << format(*ith);
		if (itb->empty()) 	ss << L".";
		++n;
	}
	return ss.str();
}

void printr(int r, const ruleset& kb) {
	dout << format(r, kb);
}

void printg(const ground& g, const ruleset& kb) {
	for (auto x : g) {
		dout << indent();
		printr_substs(x.first, x.second, kb);
		dout << std::endl;
	}
}

void printe(const evidence& e, const ruleset& kb) {
	for (auto y : e)
		for (auto x : y.second) {
			dout << indent();
			printterm(x.first);
			dout << L":" << std::endl;
			++_indent;
			printg(x.second, kb);
			--_indent;
			dout << std::endl;
		}
}
}

termid mkterm(const wchar_t* p, const wchar_t* s, const wchar_t* o, const quad& q) {
	termid ps = s ? prover::term::make(dict.set(s), 0, 0, q.subj) : 0;
	termid po = o ? prover::term::make(dict.set(o), 0, 0, q.object) : 0;
	return prover::term::make(dict.set(string(p)), ps, po, q.pred);
}

termid mkterm(string s, string p, string o, const quad& q) {
	return mkterm(p.c_str(), s.c_str(), o.c_str(), q);
}

termid quad2term(const quad& p) {
	return mkterm(p.subj->value, p.pred->value, p.object->value, p);
}

void addrules(pquad q, prover::session& ss, const qdb& kb) {
//	prover::rule* r = &prover::rules[prover::nrules++];
//	r->p = quad2term(*q);
	const string &s = q->subj->value, &p = q->pred->value, &o = q->object->value;
	uint n = ss.kb.head.size();
	ss.kb.body.push_back(prover::termset());
	if ( p != implication || kb.find ( o ) == kb.end() ) 
		ss.kb.head.push_back(quad2term(*q));
//		ss.kb/*[r->p->p]*/.insert(r);
	else for ( jsonld::pquad y : *kb.at ( o ) ) {
//		r = &prover::rules[prover::nrules++];
//		r->p = quad2term( *y );
		ss.kb.head.push_back(quad2term(*y));
		if ( kb.find ( s ) != kb.end() )
			for ( jsonld::pquad z : *kb.at ( s ) )
				ss.kb.body[n].push_back( quad2term( *z ) );
//		ss.kb/*[r->p->p]*/.insert(r);
	}
}

qlist merge ( const qdb& q ) {
	qlist r;
	for ( auto x : q ) for ( auto y : *x.second ) r.push_back ( y );
	return r;
}

bool reasoner::prove ( qdb kb, qlist query ) {
	prover::session ss;
	//std::set<string> predicates;
	for ( jsonld::pquad quad : *kb.at(L"@default")) 
		addrules(quad, ss, kb);
//		const string &s = quad->subj->value, &p = quad->pred->value, &o = quad->object->value;
//			addrules(s, p, o, ss, kb);
	for ( auto q : query )
		ss.goal.push_back( quad2term( *q ) );
	prover::prove(ss);
	return ss.e.size();
}

bool reasoner::test_reasoner() {
	dout << L"to perform the socrates test, put the following three lines in a file say f, and run \"tau < f\":" << std::endl
		<<L"socrates a man.  ?x a man _:b0.  ?x a mortal _:b1.  _:b0 => _:b1." << std::endl<<L"fin." << std::endl<<L"?p a mortal." << std::endl;
	return 1;
}

float degrees ( float f ) {
	static float pi = acos ( -1 );
	return f * 180 / pi;
}
