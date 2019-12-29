#include "noaaproductlist.h"

NoaaProductList::NoaaProductList() {}

const NoaaProductList::NoaaProduct &NoaaProductList::product(
    size_t index) const {
  return this->m_products[index];
}

QStringList NoaaProductList::productList() const {
  QStringList plist;
  for (auto &p : this->m_products) {
    plist << QString::fromStdString(p.productName());
  }
  return plist;
}
