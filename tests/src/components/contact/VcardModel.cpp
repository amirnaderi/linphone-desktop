#include <belcard/belcard.hpp>
#include <QFileInfo>
#include <QImageReader>
#include <QtDebug>
#include <QUuid>

#include "../../app/App.hpp"
#include "../../app/Database.hpp"
#include "../../utils.hpp"
#include "../core/CoreManager.hpp"

#include "VcardModel.hpp"

#define VCARD_SCHEME "linphone-desktop:/"

using namespace std;

// =============================================================================

template<class T>
inline shared_ptr<T> findBelCardValue (const list<shared_ptr<T> > &list, const QString &value) {
  string match = ::Utils::qStringToLinphoneString(value);

  auto it = find_if(
      list.cbegin(), list.cend(), [&match](const shared_ptr<T> &entry) {
        return match == entry->getValue();
      }
    );

  if (it != list.cend())
    return *it;

  return nullptr;
}

inline shared_ptr<belcard::BelCardPhoto> findBelCardPhoto (const list<shared_ptr<belcard::BelCardPhoto> > &photos) {
  auto it = find_if(
      photos.cbegin(), photos.cend(), [](const shared_ptr<belcard::BelCardPhoto> &photo) {
        return !photo->getValue().compare(0, sizeof(VCARD_SCHEME) - 1, VCARD_SCHEME);
      }
    );

  if (it != photos.cend())
    return *it;

  return nullptr;
}

// -----------------------------------------------------------------------------

VcardModel::~VcardModel () {
  // If it's a detached Vcard, the linked photo must be destroyed from fs.
  if (App::getInstance()->getEngine()->objectOwnership(this) != QQmlEngine::CppOwnership) {
    shared_ptr<belcard::BelCardPhoto> photo(findBelCardPhoto(m_vcard->getBelcard()->getPhotos()));
    if (!photo)
      return;

    QString image_path(
      ::Utils::linphoneStringToQString(
        Database::getAvatarsPath() +
        photo->getValue().substr(sizeof(VCARD_SCHEME) - 1)
      )
    );

    if (!QFile::remove(image_path))
      qWarning() << QStringLiteral("Unable to remove `%1`.").arg(image_path);
  }
}

// -----------------------------------------------------------------------------

QString VcardModel::getUsername () const {
  return ::Utils::linphoneStringToQString(m_vcard->getFullName());
}

void VcardModel::setUsername (const QString &username) {
  if (username.length() == 0 || username == getUsername())
    return;

  m_vcard->setFullName(::Utils::qStringToLinphoneString(username));
  emit vcardUpdated();
}

// -----------------------------------------------------------------------------

QString VcardModel::getAvatar () const {
  // Find desktop avatar.
  list<shared_ptr<belcard::BelCardPhoto> > photos = m_vcard->getBelcard()->getPhotos();
  shared_ptr<belcard::BelCardPhoto> photo = findBelCardPhoto(photos);

  // No path found.
  if (!photo)
    return "";

  // Returns right path.
  return QStringLiteral("image://%1/%2").arg(AvatarProvider::PROVIDER_ID).arg(
    ::Utils::linphoneStringToQString(photo->getValue().substr(sizeof(VCARD_SCHEME) - 1))
  );
}

bool VcardModel::setAvatar (const QString &path) {
  // 1. Try to copy photo in avatars folder.
  QFile file(path);

  if (!file.exists() || QImageReader::imageFormat(path).size() == 0)
    return false;

  QFileInfo info(file);
  QString uuid = QUuid::createUuid().toString();
  QString file_id = QStringLiteral("%1.%2")
    .arg(uuid.mid(1, uuid.length() - 2)) // Remove `{}`.
    .arg(info.suffix());

  QString dest = ::Utils::linphoneStringToQString(Database::getAvatarsPath()) + file_id;

  if (!file.copy(dest))
    return false;

  qInfo() << QStringLiteral("Update avatar of `%1`. (path=%2)").arg(getUsername()).arg(dest);

  // 2. Edit vcard.
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  list<shared_ptr<belcard::BelCardPhoto> > photos = belcard->getPhotos();

  // 3. Remove oldest photo.
  shared_ptr<belcard::BelCardPhoto> old_photo = findBelCardPhoto(photos);
  if (old_photo) {
    QString image_path(
      ::Utils::linphoneStringToQString(
        Database::getAvatarsPath() + old_photo->getValue().substr(sizeof(VCARD_SCHEME) - 1)
      )
    );

    if (!QFile::remove(image_path))
      qWarning() << QStringLiteral("Unable to remove `%1`.").arg(image_path);
    belcard->removePhoto(old_photo);
  }

  // 4. Update.
  shared_ptr<belcard::BelCardPhoto> photo = belcard::BelCardGeneric::create<belcard::BelCardPhoto>();
  photo->setValue(VCARD_SCHEME + ::Utils::qStringToLinphoneString(file_id));

  if (!belcard->addPhoto(photo))
    return false;

  emit vcardUpdated();
  return true;
}

// -----------------------------------------------------------------------------

QVariantMap VcardModel::getAddress () const {
  list<shared_ptr<belcard::BelCardAddress> > addresses = m_vcard->getBelcard()->getAddresses();
  QVariantMap map;

  if (addresses.empty())
    return map;

  shared_ptr<belcard::BelCardAddress> address = addresses.front();
  map["street"] = ::Utils::linphoneStringToQString(address->getStreet());
  map["locality"] = ::Utils::linphoneStringToQString(address->getLocality());
  map["postalCode"] = ::Utils::linphoneStringToQString(address->getPostalCode());
  map["country"] = ::Utils::linphoneStringToQString(address->getCountry());

  return map;
}

bool VcardModel::setAddress (const QVariantMap &address) {
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  list<shared_ptr<belcard::BelCardAddress> > addresses = belcard->getAddresses();

  while (!addresses.empty())
    belcard->removeAddress(addresses.front());

  shared_ptr<belcard::BelCardAddress> belcard_address = belcard::BelCardGeneric::create<belcard::BelCardAddress>();

  belcard_address->setStreet(::Utils::qStringToLinphoneString(address["street"].toString()));
  belcard_address->setLocality(::Utils::qStringToLinphoneString(address["locality"].toString()));
  belcard_address->setPostalCode(::Utils::qStringToLinphoneString(address["postalCode"].toString()));
  belcard_address->setCountry(::Utils::qStringToLinphoneString(address["country"].toString()));

  if (!belcard->addAddress(belcard_address))
    return false;

  emit vcardUpdated();
  return true;
}

QVariantList VcardModel::getSipAddresses () const {
  shared_ptr<linphone::Core> core = CoreManager::getInstance()->getCore();
  QVariantList list;

  for (const auto &address : m_vcard->getBelcard()->getImpp()) {
    string value = address->getValue();
    shared_ptr<linphone::Address> l_address = core->createAddress(value);

    if (l_address)
      list.append(::Utils::linphoneStringToQString(l_address->asStringUriOnly()));
  }

  return list;
}

bool VcardModel::addSipAddress (const QString &sip_address) {
  // Check sip address format.
  shared_ptr<linphone::Address> l_address = CoreManager::getInstance()->getCore()->interpretUrl(
      ::Utils::qStringToLinphoneString(sip_address)
    );

  if (!l_address) {
    qWarning() << QStringLiteral("Unable to add invalid sip address: `%1`.").arg(sip_address);
    return false;
  }

  // Add sip address in belcard.
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  shared_ptr<belcard::BelCardImpp> value = belcard::BelCardGeneric::create<belcard::BelCardImpp>();
  value->setValue(l_address->asStringUriOnly());

  qInfo() << QStringLiteral("Add new sip address: `%1`.").arg(sip_address);

  if (!belcard->addImpp(value)) {
    qWarning() << QStringLiteral("Unable to add sip address: `%1`.").arg(sip_address);
    return false;
  }

  emit vcardUpdated();
  return true;
}

void VcardModel::removeSipAddress (const QString &sip_address) {
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  list<shared_ptr<belcard::BelCardImpp> > addresses = belcard->getImpp();
  shared_ptr<belcard::BelCardImpp> value = findBelCardValue(addresses, sip_address);

  if (!value) {
    qWarning() << QStringLiteral("Unable to remove sip address: `%1`.").arg(sip_address);
    return;
  }

  if (addresses.size() == 1) {
    qWarning() << QStringLiteral("Unable to remove the only existing sip address: `%1`.")
      .arg(sip_address);
    return;
  }

  qInfo() << QStringLiteral("Remove sip address: `%1`.").arg(sip_address);
  belcard->removeImpp(value);

  emit vcardUpdated();
}

bool VcardModel::updateSipAddress (const QString &old_sip_address, const QString &sip_address) {
  if (old_sip_address == sip_address || !addSipAddress(sip_address))
    return false;

  removeSipAddress(old_sip_address);

  return true;
}

// -----------------------------------------------------------------------------

QVariantList VcardModel::getCompanies () const {
  QVariantList list;

  for (const auto &company : m_vcard->getBelcard()->getRoles())
    list.append(::Utils::linphoneStringToQString(company->getValue()));

  return list;
}

bool VcardModel::addCompany (const QString &company) {
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  shared_ptr<belcard::BelCardRole> value = belcard::BelCardGeneric::create<belcard::BelCardRole>();
  value->setValue(::Utils::qStringToLinphoneString(company));

  qInfo() << QStringLiteral("Add new company: `%1`.").arg(company);

  if (!belcard->addRole(value)) {
    qWarning() << QStringLiteral("Unable to add company: `%1`.").arg(company);
    return false;
  }

  emit vcardUpdated();
  return true;
}

void VcardModel::removeCompany (const QString &company) {
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  shared_ptr<belcard::BelCardRole> value = findBelCardValue(belcard->getRoles(), company);

  if (!value) {
    qWarning() << QStringLiteral("Unable to remove company: `%1`.").arg(company);
    return;
  }

  qInfo() << QStringLiteral("Remove company: `%1`.").arg(company);
  belcard->removeRole(value);

  emit vcardUpdated();
}

bool VcardModel::updateCompany (const QString &old_company, const QString &company) {
  if (old_company == company || !addCompany(company))
    return false;

  removeCompany(old_company);

  return true;
}

// -----------------------------------------------------------------------------

QVariantList VcardModel::getEmails () const {
  QVariantList list;

  for (const auto &email : m_vcard->getBelcard()->getEmails())
    list.append(::Utils::linphoneStringToQString(email->getValue()));

  return list;
}

bool VcardModel::addEmail (const QString &email) {
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  shared_ptr<belcard::BelCardEmail> value = belcard::BelCardGeneric::create<belcard::BelCardEmail>();
  value->setValue(::Utils::qStringToLinphoneString(email));

  qInfo() << QStringLiteral("Add new email: `%1`.").arg(email);

  if (!belcard->addEmail(value)) {
    qWarning() << QStringLiteral("Unable to add email: `%1`.").arg(email);
    return false;
  }

  emit vcardUpdated();
  return true;
}

void VcardModel::removeEmail (const QString &email) {
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  shared_ptr<belcard::BelCardEmail> value = findBelCardValue(belcard->getEmails(), email);

  if (!value) {
    qWarning() << QStringLiteral("Unable to remove email: `%1`.").arg(email);
    return;
  }

  qInfo() << QStringLiteral("Remove email: `%1`.").arg(email);
  belcard->removeEmail(value);

  emit vcardUpdated();
}

bool VcardModel::updateEmail (const QString &old_email, const QString &email) {
  if (old_email == email || !addEmail(email))
    return false;

  removeEmail(old_email);

  return true;
}

// -----------------------------------------------------------------------------

QVariantList VcardModel::getUrls () const {
  QVariantList list;

  for (const auto &url : m_vcard->getBelcard()->getURLs())
    list.append(::Utils::linphoneStringToQString(url->getValue()));

  return list;
}

bool VcardModel::addUrl (const QString &url) {
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  shared_ptr<belcard::BelCardURL> value = belcard::BelCardGeneric::create<belcard::BelCardURL>();
  value->setValue(::Utils::qStringToLinphoneString(url));

  qInfo() << QStringLiteral("Add new url: `%1`.").arg(url);

  if (!belcard->addURL(value)) {
    qWarning() << QStringLiteral("Unable to add url: `%1`.").arg(url);
    return false;
  }

  emit vcardUpdated();
  return true;
}

void VcardModel::removeUrl (const QString &url) {
  shared_ptr<belcard::BelCard> belcard = m_vcard->getBelcard();
  shared_ptr<belcard::BelCardURL> value = findBelCardValue(belcard->getURLs(), url);

  if (!value) {
    qWarning() << QStringLiteral("Unable to remove url: `%1`.").arg(url);
    return;
  }

  qInfo() << QStringLiteral("Remove url: `%1`.").arg(url);
  belcard->removeURL(value);

  emit vcardUpdated();
}

bool VcardModel::updateUrl (const QString &old_url, const QString &url) {
  if (old_url == url || !addUrl(url))
    return false;

  removeUrl(old_url);

  return true;
}
