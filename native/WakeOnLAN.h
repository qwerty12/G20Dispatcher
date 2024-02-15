#ifndef WAKEONLAN_H
#define WAKEONLAN_H

#ifdef __cplusplus
extern "C" {
#endif

void WakeOnLAN(const char *mac_addr, const char *broadcast_addr);

#ifdef __cplusplus
}
#endif

#endif /*WAKEONLAN_H*/