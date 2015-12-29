/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


#include "sx_elem.hpp"
#include "../matrix/matrix.hpp"
#include <stack>
#include <cassert>
#include "../casadi_math.hpp"
#include "constant_sx.hpp"
#include "symbolic_sx.hpp"
#include "unary_sx.hpp"
#include "binary_sx.hpp"
#include "../casadi_options.hpp"
#include "../function/sx_function.hpp"

using namespace std;
namespace casadi {

  // Allocate storage for the caching
  CACHING_MAP<int, IntegerSX*> IntegerSX::cached_constants_;
  CACHING_MAP<double, RealtypeSX*> RealtypeSX::cached_constants_;

  SXElem::SXElem() {
    node = casadi_limits<SXElem>::nan.node;
    node->count++;
  }

  SXElem::SXElem(SXNode* node_, bool dummy) : node(node_) {
    node->count++;
  }

  SXElem SXElem::create(SXNode* node) {
    return SXElem(node, false);
  }

  SXElem::SXElem(const SXElem& scalar) {
    node = scalar.node;
    node->count++;
  }

  SXElem::SXElem(double val) {
    int intval = static_cast<int>(val);
    if (val-intval == 0) { // check if integer
      if (intval == 0)             node = casadi_limits<SXElem>::zero.node;
      else if (intval == 1)        node = casadi_limits<SXElem>::one.node;
      else if (intval == 2)        node = casadi_limits<SXElem>::two.node;
      else if (intval == -1)       node = casadi_limits<SXElem>::minus_one.node;
      else                        node = IntegerSX::create(intval);
      node->count++;
    } else {
      if (isnan(val))              node = casadi_limits<SXElem>::nan.node;
      else if (isinf(val))         node = val > 0 ? casadi_limits<SXElem>::inf.node :
                                      casadi_limits<SXElem>::minus_inf.node;
      else                        node = RealtypeSX::create(val);
      node->count++;
    }
  }

  SXElem SXElem::sym(const std::string& name) {
    return create(new SymbolicSX(name));
  }

  SXElem::~SXElem() {
    if (--node->count == 0) delete node;
  }

  SXElem& SXElem::operator=(const SXElem &scalar) {
    // quick return if the old and new pointers point to the same object
    if (node == scalar.node) return *this;

    // decrease the counter and delete if this was the last pointer
    if (--node->count == 0) delete node;

    // save the new pointer
    node = scalar.node;
    node->count++;
    return *this;
  }

  void SXElem::assignIfDuplicate(const SXElem& scalar, int depth) {
    casadi_assert(depth>=1);
    if (!is_equal(*this, scalar, 0) && is_equal(*this, scalar, depth)) {
      *this = scalar;
    }
  }

  SXNode* SXElem::assignNoDelete(const SXElem& scalar) {
    // Return value
    SXNode* ret = node;

    // quick return if the old and new pointers point to the same object
    if (node == scalar.node) return ret;

    // decrease the counter but do not delete if this was the last pointer
    --node->count;

    // save the new pointer
    node = scalar.node;
    node->count++;

    // Return a pointer to the old node
    return ret;
  }

  SXElem& SXElem::operator=(double scalar) {
    return *this = SXElem(scalar);
  }

  void SXElem::repr(std::ostream &stream, bool trailing_newline) const {
    print(stream, trailing_newline);
  }

  void SXElem::print(std::ostream &stream, bool trailing_newline) const {
    node->print(stream);
    if (trailing_newline) stream << std::endl;
  }

  SXElem SXElem::operator-() const {
    if (is_op(OP_NEG))
      return dep();
    else if (is_zero())
      return 0;
    else if (is_minus_one())
      return 1;
    else if (is_one())
      return -1;
    else
      return UnarySX::create(OP_NEG, *this);
  }

  bool SXElem::__nonzero__() const {
    if (is_constant()) return !is_zero();
    casadi_error("Cannot compute the truth value of a CasADi SXElem symbolic expression.")
  }

  bool SXElem::isDoubled() const {
    return is_op(OP_ADD) && is_equal(dep(0), dep(1), SXNode::eq_depth_);
  }

  SXElem SXElem::inv() const {
    if (is_op(OP_INV)) {
      return dep(0);
    } else {
      return UnarySX::create(OP_INV, *this);
    }
  }

  SXElem SXElem::zz_le(const SXElem& y) const {
    if ((y-(*this)).isNonNegative())
      return 1;
    else
      return BinarySX::create(OP_LE, *this, y);
  }

  SXElem SXElem::zz_lt(const SXElem& y) const {
    if (((*this)-y).isNonNegative())
      return 0;
    else
      return BinarySX::create(OP_LT, *this, y);
  }

  SXElem SXElem::zz_eq(const SXElem& y) const {
    if (is_equal(*this, y))
      return 1;
    else
      return BinarySX::create(OP_EQ, *this, y);
  }

  SXElem SXElem::zz_ne(const SXElem& y) const {
    if (is_equal(*this, y))
      return 0;
    else
      return BinarySX::create(OP_NE, *this, y);
  }

  SXNode* SXElem::get() const {
    return node;
  }

  const SXNode* SXElem::operator->() const {
    return node;
  }

  SXNode* SXElem::operator->() {
    return node;
  }

  SXElem if_else(const SXElem& cond, const SXElem& if_true,
                    const SXElem& if_false, bool short_circuit) {
    return if_else_zero(cond, if_true) + if_else_zero(!cond, if_false);
  }

  SXElem SXElem::binary(int op, const SXElem& x, const SXElem& y) {
    // Simplifications
    if (CasadiOptions::simplification_on_the_fly) {
      switch (op) {
      case OP_ADD:
        if (x.is_zero())
          return y;
        else if (y->is_zero()) // term2 is zero
          return x;
        else if (y.is_op(OP_NEG))  // x + (-y) -> x - y
          return x - (-y);
        else if (x.is_op(OP_NEG)) // (-x) + y -> y - x
          return y - x.dep();
        else if (x.is_op(OP_MUL) && y.is_op(OP_MUL) &&
                 x.dep(0).is_constant() && x.dep(0).getValue()==0.5 &&
                 y.dep(0).is_constant() && y.dep(0).getValue()==0.5 &&
                 is_equal(y.dep(1), x.dep(1), SXNode::eq_depth_)) // 0.5x+0.5x = x
          return x.dep(1);
        else if (x.is_op(OP_DIV) && y.is_op(OP_DIV) &&
                 x.dep(1).is_constant() && x.dep(1).getValue()==2 &&
                 y.dep(1).is_constant() && y.dep(1).getValue()==2 &&
                 is_equal(y.dep(0), x.dep(0), SXNode::eq_depth_)) // x/2+x/2 = x
          return x.dep(0);
        else if (x.is_op(OP_SUB) && is_equal(x.dep(1), y, SXNode::eq_depth_))
          return x.dep(0);
        else if (y.is_op(OP_SUB) && is_equal(x, y.dep(1), SXNode::eq_depth_))
          return y.dep(0);
        else if (x.is_op(OP_SQ) && y.is_op(OP_SQ) &&
                 ((x.dep().is_op(OP_SIN) && y.dep().is_op(OP_COS))
                  || (x.dep().is_op(OP_COS) && y.dep().is_op(OP_SIN)))
                 && is_equal(x.dep().dep(), y.dep().dep(), SXNode::eq_depth_))
          return 1; // sin^2 + cos^2 -> 1
        break;
      case OP_SUB:
        if (y->is_zero()) // term2 is zero
          return x;
        if (x.is_zero()) // term1 is zero
          return -y;
        if (is_equal(x, y, SXNode::eq_depth_)) // the terms are equal
          return 0;
        else if (y.is_op(OP_NEG)) // x - (-y) -> x + y
          return x + y.dep();
        else if (x.is_op(OP_ADD) && is_equal(x.dep(1), y, SXNode::eq_depth_))
          return x.dep(0);
        else if (x.is_op(OP_ADD) && is_equal(x.dep(0), y, SXNode::eq_depth_))
          return x.dep(1);
        else if (y.is_op(OP_ADD) && is_equal(x, y.dep(1), SXNode::eq_depth_))
          return -y.dep(0);
        else if (y.is_op(OP_ADD) && is_equal(x, y.dep(0), SXNode::eq_depth_))
          return -y.dep(1);
        else if (x.is_op(OP_NEG))
          return -(x.dep() + y);
        break;
      case OP_MUL:
        if (is_equal(y, x, SXNode::eq_depth_))
          return x.sq();
        else if (!x.is_constant() && y.is_constant())
          return y * x;
        else if (x.is_zero() || y->is_zero()) // one of the terms is zero
          return 0;
        else if (x.is_one()) // term1 is one
          return y;
        else if (y->is_one()) // term2 is one
          return x;
        else if (y->is_minus_one())
          return -x;
        else if (x.is_minus_one())
          return -y;
        else if (y.is_op(OP_INV))
          return x/y.inv();
        else if (x.is_op(OP_INV))
          return y / x.inv();
        else if (x.is_constant() && y.is_op(OP_MUL) && y.dep(0).is_constant() &&
                 x.getValue()*y.dep(0).getValue()==1) // 5*(0.2*x) = x
          return y.dep(1);
        else if (x.is_constant() && y.is_op(OP_DIV) && y.dep(1).is_constant() &&
                 x.getValue()==y.dep(1).getValue()) // 5*(x/5) = x
          return y.dep(0);
        else if (x.is_op(OP_DIV) && is_equal(x.dep(1), y, SXNode::eq_depth_)) // ((2/x)*x)
          return x.dep(0);
        else if (y.is_op(OP_DIV) &&
                 is_equal(y.dep(1), x, SXNode::eq_depth_)) // ((2/x)*x)
          return y.dep(0);
        else if (x.is_op(OP_NEG))
          return -(x.dep() * y);
        else if (y.is_op(OP_NEG))
          return -(x * y.dep());
        break;
      case OP_DIV:
        if (y->is_zero()) // term2 is zero
          return casadi_limits<SXElem>::nan;
        else if (x.is_zero()) // term1 is zero
          return 0;
        else if (y->is_one()) // term2 is one
          return x;
        else if (y->is_minus_one())
          return -x;
        else if (is_equal(x, y, SXNode::eq_depth_)) // terms are equal
          return 1;
        else if (x.isDoubled() && is_equal(y, 2))
          return x.dep(0);
        else if (x.is_op(OP_MUL) && is_equal(y, x.dep(0), SXNode::eq_depth_))
          return x.dep(1);
        else if (x.is_op(OP_MUL) && is_equal(y, x.dep(1), SXNode::eq_depth_))
          return x.dep(0);
        else if (x.is_one())
          return y.inv();
        else if (y.is_op(OP_INV))
          return x*y.inv();
        else if (x.isDoubled() && y.isDoubled())
          return x.dep(0) / y->dep(0);
        else if (y.is_constant() && x.is_op(OP_DIV) && x.dep(1).is_constant() &&
                 y.getValue()*x.dep(1).getValue()==1) // (x/5)/0.2
          return x.dep(0);
        else if (y.is_op(OP_MUL) &&
                 is_equal(y.dep(1), x, SXNode::eq_depth_)) // x/(2*x) = 1/2
          return BinarySX::create(OP_DIV, 1, y.dep(0));
        else if (x.is_op(OP_NEG) &&
                 is_equal(x.dep(0), y, SXNode::eq_depth_))      // (-x)/x = -1
          return -1;
        else if (y.is_op(OP_NEG) &&
                 is_equal(y.dep(0), x, SXNode::eq_depth_))      // x/(-x) = 1
          return -1;
        else if (y.is_op(OP_NEG) && x.is_op(OP_NEG) &&
                 is_equal(x.dep(0), y.dep(0), SXNode::eq_depth_))  // (-x)/(-x) = 1
          return 1;
        else if (x.is_op(OP_DIV) && is_equal(y, x.dep(0), SXNode::eq_depth_))
          return x.dep(1).inv();
        else if (x.is_op(OP_NEG))
          return -(x.dep() / y);
        else if (y.is_op(OP_NEG))
          return -(x / y.dep());
        break;
      }
    }
    return BinarySX::create(Operation(op), x, y);
  }

  SXElem SXElem::unary(int op, const SXElem& x) {
    return UnarySX::create(Operation(op), x);
  }

  bool SXElem::is_leaf() const {
    if (!node) return true;
    return is_constant() || is_symbolic();
  }

  bool SXElem::is_commutative() const {
    if (!hasDep()) throw CasadiException("SX::is_commutative: must be binary");
    return operation_checker<CommChecker>(op());
  }

  bool SXElem::is_constant() const {
    return node->is_constant();
  }

  bool SXElem::is_integer() const {
    return node->is_integer();
  }

  bool SXElem::is_symbolic() const {
    return node->is_symbolic();
  }

  bool SXElem::hasDep() const {
    return node->hasDep();
  }

  bool SXElem::is_zero() const {
    return node->is_zero();
  }

  bool SXElem::isAlmostZero(double tol) const {
    return node->isAlmostZero(tol);
  }

  bool SXElem::is_one() const {
    return node->is_one();
  }

  bool SXElem::is_minus_one() const {
    return node->is_minus_one();
  }

  bool SXElem::isNan() const {
    return node->isNan();
  }

  bool SXElem::isInf() const {
    return node->isInf();
  }

  bool SXElem::isMinusInf() const {
    return node->isMinusInf();
  }

  const std::string& SXElem::getName() const {
    return node->getName();
  }

  int SXElem::op() const {
    return node->op();
  }

  bool SXElem::is_op(int op) const {
    return hasDep() && op==this->op();
  }

  bool SXElem::zz_is_equal(const SXElem& ex, int depth) const {
    if (node==ex.get())
      return true;
    else if (depth>0)
      return node->zz_is_equal(ex.get(), depth);
    else
      return false;
  }

  bool SXElem::isNonNegative() const {
    if (is_constant())
      return getValue()>=0;
    else if (is_op(OP_SQ) || is_op(OP_FABS))
      return true;
    else
      return false;
  }

  double SXElem::getValue() const {
    return node->getValue();
  }

  int SXElem::getIntValue() const {
    return node->getIntValue();
  }

  SXElem SXElem::dep(int ch) const {
    casadi_assert(ch==0 || ch==1;)
      return node->dep(ch);
  }

  int SXElem::getNdeps() const {
    if (!hasDep()) throw CasadiException("SX::getNdeps: must be binary");
    return casadi_math<double>::ndeps(op());
  }

  size_t SXElem::__hash__() const {
    return reinterpret_cast<size_t>(node);
  }

  // node corresponding to a constant 0
  const SXElem casadi_limits<SXElem>::zero(new ZeroSX(), false);
  // node corresponding to a constant 1
  const SXElem casadi_limits<SXElem>::one(new OneSX(), false);
  // node corresponding to a constant 2
  const SXElem casadi_limits<SXElem>::two(IntegerSX::create(2), false);
  // node corresponding to a constant -1
  const SXElem casadi_limits<SXElem>::minus_one(new MinusOneSX(), false);
  const SXElem casadi_limits<SXElem>::nan(new NanSX(), false);
  const SXElem casadi_limits<SXElem>::inf(new InfSX(), false);
  const SXElem casadi_limits<SXElem>::minus_inf(new MinusInfSX(), false);

  bool casadi_limits<SXElem>::is_zero(const SXElem& val) {
    return val.is_zero();
  }

  bool casadi_limits<SXElem>::isAlmostZero(const SXElem& val, double tol) {
    return val.isAlmostZero(tol);
  }

  bool casadi_limits<SXElem>::is_one(const SXElem& val) {
    return val.is_one();
  }

  bool casadi_limits<SXElem>::is_minus_one(const SXElem& val) {
    return val.is_minus_one();
  }

  bool casadi_limits<SXElem>::is_constant(const SXElem& val) {
    return val.is_constant();
  }

  bool casadi_limits<SXElem>::is_integer(const SXElem& val) {
    return val.is_integer();
  }

  bool casadi_limits<SXElem>::isInf(const SXElem& val) {
    return val.isInf();
  }

  bool casadi_limits<SXElem>::isMinusInf(const SXElem& val) {
    return val.isMinusInf();
  }

  bool casadi_limits<SXElem>::isNaN(const SXElem& val) {
    return val.isNan();
  }

  SXElem SXElem::zz_exp() const {
    return UnarySX::create(OP_EXP, *this);
  }

  SXElem SXElem::zz_log() const {
    return UnarySX::create(OP_LOG, *this);
  }

  SXElem SXElem::zz_log10() const {
    return log(*this)*(1/std::log(10.));
  }

  SXElem SXElem::zz_sqrt() const {
    if (is_op(OP_SQ))
      return fabs(dep());
    else
      return UnarySX::create(OP_SQRT, *this);
  }

  SXElem SXElem::sq() const {
    if (is_op(OP_SQRT))
      return dep();
    else if (is_op(OP_NEG))
      return dep().sq();
    else
      return UnarySX::create(OP_SQ, *this);
  }

  SXElem SXElem::zz_sin() const {
    return UnarySX::create(OP_SIN, *this);
  }

  SXElem SXElem::zz_cos() const {
    return UnarySX::create(OP_COS, *this);
  }

  SXElem SXElem::zz_tan() const {
    return UnarySX::create(OP_TAN, *this);
  }

  SXElem SXElem::zz_asin() const {
    return UnarySX::create(OP_ASIN, *this);
  }

  SXElem SXElem::zz_acos() const {
    return UnarySX::create(OP_ACOS, *this);
  }

  SXElem SXElem::zz_atan() const {
    return UnarySX::create(OP_ATAN, *this);
  }

  SXElem SXElem::zz_sinh() const {
    if (is_zero())
      return 0;
    else
      return UnarySX::create(OP_SINH, *this);
  }

  SXElem SXElem::zz_cosh() const {
    if (is_zero())
      return 1;
    else
      return UnarySX::create(OP_COSH, *this);
  }

  SXElem SXElem::zz_tanh() const {
    if (is_zero())
      return 0;
    else
      return UnarySX::create(OP_TANH, *this);
  }

  SXElem SXElem::zz_atanh() const {
    if (is_zero())
      return 0;
    else
      return UnarySX::create(OP_ATANH, *this);
  }

  SXElem SXElem::zz_acosh() const {
    if (is_one())
      return 0;
    else
      return UnarySX::create(OP_ACOSH, *this);
  }

  SXElem SXElem::zz_asinh() const {
    if (is_zero())
      return 0;
    else
      return UnarySX::create(OP_ASINH, *this);
  }

  SXElem SXElem::zz_floor() const {
    return UnarySX::create(OP_FLOOR, *this);
  }

  SXElem SXElem::zz_ceil() const {
    return UnarySX::create(OP_CEIL, *this);
  }

  SXElem SXElem::zz_mod(const SXElem &b) const {
    return BinarySX::create(OP_FMOD, *this, b);
  }

  SXElem SXElem::zz_abs() const {
    if (is_op(OP_FABS) || is_op(OP_SQ))
      return *this;
    else
      return UnarySX::create(OP_FABS, *this);
  }

  SXElem::operator SX() const {
    return SX(Sparsity::scalar(), *this, false);
  }

  SXElem SXElem::zz_min(const SXElem &b) const {
    return BinarySX::create(OP_FMIN, *this, b);
  }

  SXElem SXElem::zz_max(const SXElem &b) const {
    return BinarySX::create(OP_FMAX, *this, b);
  }

  SXElem SXElem::zz_atan2(const SXElem &b) const {
    return BinarySX::create(OP_ATAN2, *this, b);
  }

  SXElem SXElem::printme(const SXElem &b) const {
    return BinarySX::create(OP_PRINTME, *this, b);
  }

  SXElem SXElem::zz_power(const SXElem& n) const {
    if (n->is_constant()) {
      if (n->is_integer()) {
        int nn = n->getIntValue();
        if (nn == 0) {
          return 1;
        } else if (nn>100 || nn<-100) { // maximum depth
          return BinarySX::create(OP_CONSTPOW, *this, nn);
        } else if (nn<0) { // negative power
          return 1/pow(*this, -nn);
        } else if (nn%2 == 1) { // odd power
          return *this*pow(*this, nn-1);
        } else { // even power
          SXElem rt = pow(*this, nn/2);
          return rt*rt;
        }
      } else if (n->getValue()==0.5) {
        return sqrt(*this);
      } else {
        return BinarySX::create(OP_CONSTPOW, *this, n);
      }
    } else {
      return BinarySX::create(OP_POW, *this, n);
    }
  }

  SXElem SXElem::zz_constpow(const SXElem& n) const {
    return BinarySX::create(OP_CONSTPOW, *this, n);
  }

  SXElem SXElem::zz_not() const {
    if (is_op(OP_NOT)) {
      return dep();
    } else {
      return UnarySX::create(OP_NOT, *this);
    }
  }

  SXElem SXElem::zz_and(const SXElem& y) const {
    return BinarySX::create(OP_AND, *this, y);
  }

  SXElem SXElem::zz_or(const SXElem& y) const {
    return BinarySX::create(OP_OR, *this, y);
  }

  SXElem SXElem::zz_if_else_zero(const SXElem& y) const {
    if (y->is_zero()) {
      return y;
    } else if (is_constant()) {
      if (getValue()!=0) return y;
      else              return 0;
    } else {
      return BinarySX::create(OP_IF_ELSE_ZERO, *this, y);
    }
  }

  int SXElem::getTemp() const {
    return (*this)->temp;
  }

  void SXElem::setTemp(int t) {
    (*this)->temp = t;
  }

  bool SXElem::marked() const {
    return (*this)->marked();
  }

  void SXElem::mark() {
    (*this)->mark();
  }

  bool SXElem::is_regular() const {
    if (is_constant()) {
      return !(isNan() || isInf() || isMinusInf());
    } else {
      casadi_error("Cannot check regularity for symbolic SXElem");
    }
  }

} // namespace casadi

using namespace casadi;
namespace std {
/**
  const bool numeric_limits<casadi::SXElem>::is_specialized = true;
  const int  numeric_limits<casadi::SXElem>::digits = 0;
  const int  numeric_limits<casadi::SXElem>::digits10 = 0;
  const bool numeric_limits<casadi::SXElem>::is_signed = false;
  const bool numeric_limits<casadi::SXElem>::is_integer = false;
  const bool numeric_limits<casadi::SXElem>::is_exact = false;
  const int numeric_limits<casadi::SXElem>::radix = 0;
  const int  numeric_limits<casadi::SXElem>::min_exponent = 0;
  const int  numeric_limits<casadi::SXElem>::min_exponent10 = 0;
  const int  numeric_limits<casadi::SXElem>::max_exponent = 0;
  const int  numeric_limits<casadi::SXElem>::max_exponent10 = 0;

  const bool numeric_limits<casadi::SXElem>::has_infinity = true;
  const bool numeric_limits<casadi::SXElem>::has_quiet_NaN = true;
  const bool numeric_limits<casadi::SXElem>::has_signaling_NaN = false;
  const float_denorm_style has_denorm = denorm absent;
  const bool numeric_limits<casadi::SXElem>::has_denorm_loss = false;

  const bool numeric_limits<casadi::SXElem>::is_iec559 = false;
  const bool numeric_limits<casadi::SXElem>::is_bounded = false;
  const bool numeric_limits<casadi::SXElem>::is_modulo = false;

  const bool numeric_limits<casadi::SXElem>::traps = false;
  const bool numeric_limits<casadi::SXElem>::tinyness_before = false;
  const float_round_style numeric_limits<casadi::SXElem>::round_style = round_toward_zero;
*/
  SXElem numeric_limits<SXElem>::infinity() throw() {
    return casadi::casadi_limits<SXElem>::inf;
  }

  SXElem numeric_limits<SXElem>::quiet_NaN() throw() {
    return casadi::casadi_limits<SXElem>::nan;
  }

  SXElem numeric_limits<SXElem>::min() throw() {
    return SXElem(numeric_limits<double>::min());
  }

  SXElem numeric_limits<SXElem>::max() throw() {
    return SXElem(numeric_limits<double>::max());
  }

  SXElem numeric_limits<SXElem>::epsilon() throw() {
    return SXElem(numeric_limits<double>::epsilon());
  }

  SXElem numeric_limits<SXElem>::round_error() throw() {
    return SXElem(numeric_limits<double>::round_error());
  }

} // namespace std

