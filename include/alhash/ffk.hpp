/* find first k, example template */
/*
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 */

#ifndef __ALH_HPP
#define __ALH_HPP

template<typename _Tp>
long
__med3(long xp, long yp, long zp, const std::vector<_Tp> &_eV)
{
  return (_eV[xp] < _eV[yp]) ?
         ((_eV[yp] < _eV[zp]) ? yp : ((_eV[xp] < _eV[zp]) ? zp : xp))
        :((_eV[yp] > _eV[zp]) ? yp : ((_eV[xp] < _eV[zp]) ? xp : zp));
}

template<typename _Tp>
void
ffk(std::vector<_Tp> &_eV, long topn)
{
  long lv = 0;
  long rv = _eV.size() - 1;
  long topnp = topn - 1;

  while (lv < rv) {
    long w = rv - lv;

    if (7 < w) {
      long lt  = lv;
      long mid = lt + (w / 2);
      long rt  = rv;
      if (40  < w) {
	long d = w / 8;
	lt  = __med3(lt,         lt + d, lt + 2 * d, _eV);
	mid = __med3(mid - d,    mid,    mid + d,    _eV);
	rt  = __med3(rt - 2 * d, rt - d, rt,         _eV);
      }
      long pvi = __med3(lt, mid, rt, _eV);
      std::swap(_eV[pvi], _eV[rv]);
    }
    long lp = lv - 1;
    long rp = rv;
    for (;;) {
      do { lp++; } while (_eV[lp] < _eV[rv]);
      do { rp--; } while (lp < rp && (_eV[rp] > _eV[rv]));
      if (lp >= rp) break;
      std::swap(_eV[lp], _eV[rp]);
    }
    std::swap(_eV[rv], _eV[lp]);

    if (lp == topnp) break;

    if (topnp <= lp) rv = lp - 1;
    if (topnp >= lp) lv = lp + 1;
  }
}

#endif
