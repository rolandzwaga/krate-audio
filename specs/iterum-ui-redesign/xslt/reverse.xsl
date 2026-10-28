<?xml version="1.0" encoding="UTF-8"?>
<!-- reverse.xsl - rebuilds ReversePanel exactly as specs/iterum-ui-redesign/panels/reverse.md; identity elsewhere. -->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                xmlns:saxon="http://saxon.sf.net/"
                exclude-result-prefixes="xs saxon">

  <xsl:include href="cells.xsl"/>

  <xsl:output method="xml" encoding="UTF-8" indent="yes" saxon:indent-spaces="2" saxon:line-length="100000"/>
  <xsl:strip-space elements="*"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:template match="template[@name = 'ReversePanel']">
    <template name="ReversePanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel"
              transparent="false">
      <!-- Core strip -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'ReverseChunkSize'"/>
        <xsl:with-param name="label" select="'Chunk Size'"/>
        <xsl:with-param name="sub-controller" select="'ReverseTimeSyncController'"/>
        <xsl:with-param name="toggle-tag" select="'ReverseTimeMode'"/>
        <xsl:with-param name="toggle-title" select="'Sync'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'ReverseFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'ReverseDryWet'"/>
        <xsl:with-param name="label" select="'Mix'"/>
      </xsl:call-template>

      <!-- PLAYBACK -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="580"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'PLAYBACK'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'ReversePlaybackMode'"/>
            <xsl:with-param name="label" select="'Playback Mode'"/>
            <xsl:with-param name="segments" select="'Full Reverse,Alternating,Random'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="302"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'ReverseCrossfade'"/>
            <xsl:with-param name="label" select="'Crossfade'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- FILTER -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'FILTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="toggle-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="124"/>
            <xsl:with-param name="tag" select="'ReverseFilterEnabled'"/><xsl:with-param name="opens-section" select="true()"/>
            <xsl:with-param name="label" select="'Filter'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'ReverseFilterCutoff'"/>
            <xsl:with-param name="label" select="'Filter Cutoff'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'ReverseFilterType'"/>
            <xsl:with-param name="label" select="'Filter Type'"/>
            <xsl:with-param name="segments" select="'LowPass,HighPass,BandPass'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>
</xsl:stylesheet>
