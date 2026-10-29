<?xml version="1.0" encoding="UTF-8"?>
<!--
  pingpong.xsl - rebuilds the PingPongPanel template exactly as specs/iterum-ui-redesign/panels/pingpong.md
  specifies; identity transform for everything else.
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">

  <xsl:include href="cells.xsl"/>
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:template match="template[@name = 'PingPongPanel']">
    <template name="PingPongPanel" class="CViewContainer" origin="0, 0" size="900, 480"
              background-color="panel" transparent="false">
      <!-- core 1: Time + Sync (sub-controller kept) -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'PingPongDelayTime'"/>
        <xsl:with-param name="label" select="'Time'"/>
        <xsl:with-param name="sub-controller" select="'PingPongTimeSyncController'"/>
        <xsl:with-param name="toggle-tag" select="'PingPongTimeMode'"/>
        <xsl:with-param name="toggle-title" select="'Sync'"/>
      </xsl:call-template>
      <!-- core 2: Feedback -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'PingPongFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <!-- core 3: Mix -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'PingPongMix'"/>
        <xsl:with-param name="label" select="'Mix'"/>
      </xsl:call-template>

      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="580"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'PING PONG'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'PingPongLRRatio'"/>
            <xsl:with-param name="label" select="'L/R Ratio'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="302"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'PingPongCrossFeedback'"/>
            <xsl:with-param name="label" select="'Cross-Feedback'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'MODULATION'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'PingPongModDepth'"/>
            <xsl:with-param name="label" select="'Mod Depth'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'PingPongModRate'"/>
            <xsl:with-param name="label" select="'Mod Rate'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="288"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'STEREO'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'PingPongWidth'"/>
            <xsl:with-param name="label" select="'Width'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>

</xsl:stylesheet>
