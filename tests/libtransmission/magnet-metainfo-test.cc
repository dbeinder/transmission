/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "magnet-metainfo.h"
#include "utils.h"

#include "gtest/gtest.h"

#include <array>
#include <string_view>

using namespace std::literals;

TEST(MagnetMetainfo, magnetParse)
{
    auto constexpr ExpectedHash = tr_sha1_digest_t{ tr_byte(210), tr_byte(53),  tr_byte(64),  tr_byte(16),  tr_byte(163),
                                                    tr_byte(202), tr_byte(74),  tr_byte(222), tr_byte(91),  tr_byte(116),
                                                    tr_byte(39),  tr_byte(187), tr_byte(9),   tr_byte(58),  tr_byte(98),
                                                    tr_byte(163), tr_byte(137), tr_byte(159), tr_byte(243), tr_byte(129) };

    auto constexpr UriHex =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"sv;

    auto constexpr UriBase32 =
        "magnet:?xt=urn:btih:"
        "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"sv;

    for (auto const& uri : { UriHex, UriBase32 })
    {
        auto mm = tr_magnet_metainfo{};

        EXPECT_TRUE(mm.parseMagnet(uri));
        EXPECT_EQ(2, std::size(mm.announce_list));
        auto it = std::begin(mm.announce_list);
        EXPECT_EQ(0, it->tier);
        EXPECT_EQ("http://tracker.openbittorrent.com/announce"sv, it->announce.full);
        EXPECT_EQ("http://tracker.openbittorrent.com/scrape"sv, it->scrape.full);
        ++it;
        EXPECT_EQ(1, it->tier);
        EXPECT_EQ("http://tracker.opentracker.org/announce", it->announce.full);
        EXPECT_EQ("http://tracker.opentracker.org/scrape", it->scrape.full);
        EXPECT_EQ(1, std::size(mm.webseed_urls));
        EXPECT_EQ("http://server.webseed.org/path/to/file"sv, mm.webseed_urls.front());
        EXPECT_EQ("Display Name"sv, mm.name);
        EXPECT_EQ(ExpectedHash, mm.info_hash);
    }
}
